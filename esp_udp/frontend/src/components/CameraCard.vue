<template>
  <v-card class="mb-3">
    <!-- 使用 canvas 替代 v-img 以避免 blob URL 频繁创建/撤销导致的卡顿 -->
    <div class="video-container" :style="{ position: 'relative', width: '100%', aspectRatio: `${camera.currentResolution.width} / ${camera.currentResolution.height}` }">
      <canvas ref="videoCanvasRef" class="video-canvas"
        :style="{ width: '100%', height: '100%', display: 'block' }" />
      <!-- 加载中提示（首次连接时显示） -->
      <div v-if="streamState === 'connecting'" class="d-flex align-center justify-center fill-height"
        :style="{ position: 'absolute', top: 0, left: 0, right: 0, bottom: 0, background: 'rgba(0,0,0,0.3)' }">
        <v-progress-circular color="grey-lighten-4" indeterminate />
      </div>
      <!-- 错误提示 -->
      <div v-if="streamState === 'error'" class="d-flex align-center justify-center fill-height"
        :style="{ position: 'absolute', top: 0, left: 0, right: 0, bottom: 0, background: 'rgba(0,0,0,0.3)' }">
        <div style="height: 100px;" class="d-flex flex-column">
          <div class="my-auto" style="font-size: larger; font-weight: bold; color: white;">
            Something went wrong
          </div>
          <div class="mx-auto" style="font-size: smaller; opacity: 70%; color: white;">
            Retrying in 3 seconds...
          </div>
        </div>
      </div>
    </div>
    <div class="d-flex justify-space-between align-center my-2 mx-3">
      <div>
        <div style="font-weight: bold; font-size: larger;">
          {{ camera.name && camera.name.length > 0 ? camera.name : `Camera #${camera.index}` }}
        </div>
        <div style="font-size: smaller; opacity: 70%;">
          {{ camera.currentImageFormatDescription }} @ {{ camera.currentFrameRate }} fps
          <span v-if="camera.currentQuality">
            (Quality: {{ camera.currentQuality }})
          </span>
        </div>
        <!-- 端到端延迟显示 -->
        <div style="font-size: smaller; opacity: 70%;">
          <span :style="{ color: latencyColor }">
            延迟: {{ endToEndLatencyMs }} ms
          </span>
          <span style="margin-left: 8px; opacity: 50%;">
            (服务器: {{ serverLatencyMs }} ms)
          </span>
        </div>
      </div>
      <div class="d-flex">
        <v-btn variant="tonal" @click="settingsDialog = true" :icon="mdiCog" class="mr-1" aria-label="Settings" />
        <v-btn variant="tonal" @click="captureFrame" :icon="mdiCameraOutline" class="mr-1"
          aria-label="Download Frame" />
        <v-btn variant="tonal" @click="captureRawFrame" :icon="mdiRaw" class="mr-1"
          aria-label="Download Raw Image (BIN)" />
      </div>
    </div>
  </v-card>

  <v-dialog v-model="settingsDialog" max-width="500">
    <v-card>
      <v-card-title :prepend-icon="mdiCog">
        Camera Settings
      </v-card-title>
      <v-card-text>
        <v-select v-model="selectedImageFormatId" :items="camera.imageFormats" item-title="description" item-value="id"
          :disabled="settingsSaving" label="Image Format" />
        <v-slider v-model="selectedQuality" v-if="selectedFormat?.quality" :min="selectedFormat?.quality.min ?? 80"
          :max="selectedFormat?.quality.max ?? 95" :step="selectedFormat?.quality.step ?? 1" :disabled="settingsSaving"
          label="Quality">
          <template #append>
            {{ selectedQuality }}
          </template>
        </v-slider>
        <div v-else class="text-center mb-4">This image format may not support quality settings</div>
        <v-row>
          <v-col cols="9">
            <v-btn variant="tonal" @click="saveSettings" width="100%" :loading="settingsSaving">Save</v-btn>
          </v-col>
          <v-col cols="3">
            <v-btn variant="tonal" @click="settingsDialog = false" color="error" width="100%"
              :disabled="settingsSaving">Cancel</v-btn>
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>
  </v-dialog>

  <v-snackbar v-model="saveStatusSnackbar" :timeout="2000" color="success">
    {{ saveStatusSnackbarText }}
  </v-snackbar>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { mdiCameraOutline, mdiRaw, mdiCog } from '@mdi/js';
import { useMainStore } from '@/store/mainstore';

const LOADING_IMAGE_SRC = "/loading.jpg"

const mainStore = useMainStore()

const props = defineProps<{
  camNum: number,
}>()

const camera = computed(() => mainStore.clientCameras[props.camNum])

const imgSrc = ref<string>(LOADING_IMAGE_SRC)
const settingsDialog = ref<boolean>(false)
const selectedImageFormatId = ref<number | string>(camera.value.currentImageFormat)
const selectedQuality = ref<number>(camera.value.currentQuality ?? 80)
const settingsSaving = ref<boolean>(false)
const saveStatusSnackbar = ref<boolean>(false)
const saveStatusSnackbarText = ref<string>("")
const retryTimeoutId = ref<ReturnType<typeof setTimeout> | null>(null)
const reloadTimeoutId = ref<ReturnType<typeof setTimeout> | null>(null)

/* 延迟测量相关状态 */
const endToEndLatencyMs = ref<number>(0)       // 端到端延迟 (摄像头→浏览器)
const serverLatencyMs = ref<number>(0)          // 服务器端处理延迟 (来自ESP日志)
const latencyColor = ref<string>('green')       // 延迟颜色指示

let abortController: AbortController | null = null  // 用于取消fetch流

const selectedFormat = computed(() => {
  return camera.value.imageFormats.find(format => format.id === selectedImageFormatId.value)
})

const onImageError = () => {
  if (imgSrc.value === LOADING_IMAGE_SRC) return;

  if (retryTimeoutId.value) {
    clearTimeout(retryTimeoutId.value)
  }

  retryTimeoutId.value = setTimeout(() => {
    reloadCameraSrc(1000)
    retryTimeoutId.value = null
  }, 3000)
}

const realCameraUrl = computed(() => {
  let port: number | null = null;
  let path: string | null = null;

  if (camera.value.src.startsWith(':')) {
    const [, portStr, pathStr] = camera.value.src.split(/[:\/]/);
    port = Number(portStr);
    path = pathStr;

    const realUrl = new URL(path, location.href);
    realUrl.port = port.toString();
    return realUrl.toString();
  } else {
    path = camera.value.src;
    return new URL(path, location.href).toString();
  }
})

/**
 * 解析 multipart/x-mixed-replace 流，提取 X-Timestamp 并计算延迟
 * 使用 fetch + ReadableStream 手动解析 MJPEG 流
 * 同时将 JPEG 帧数据转为 blob URL 供 <v-img> 显示
 */
const startMjpegStream = async () => {
  // 取消之前的流
  if (abortController) {
    abortController.abort();
    abortController = null;
  }

  abortController = new AbortController();

  // 用于管理 blob URL 的释放
  let currentBlobUrl: string | null = null;

  try {
    const response = await fetch(realCameraUrl.value, {
      signal: abortController.signal,
    });

    if (!response.ok || !response.body) {
      throw new Error(`HTTP ${response.status}`);
    }

    const reader = response.body.getReader();
    const boundary = '--123456789000000000000987654321';
    const boundaryMarker = '\r\n' + boundary + '\r\n';
    const boundaryEnd = boundary + '--';

    // 使用 Uint8Array 缓冲区来正确处理二进制数据
    let buffer = new Uint8Array(0);
    // 文本解码器仅用于解析头部
    const textDecoder = new TextDecoder();

    /**
     * 在 Uint8Array 中搜索子串（文本模式）
     */
    const findInBuffer = (haystack: Uint8Array, needle: string, startOffset: number = 0): number => {
      const needleBytes = new TextEncoder().encode(needle);
      for (let i = startOffset; i <= haystack.length - needleBytes.length; i++) {
        let match = true;
        for (let j = 0; j < needleBytes.length; j++) {
          if (haystack[i + j] !== needleBytes[j]) {
            match = false;
            break;
          }
        }
        if (match) return i;
      }
      return -1;
    };

    /**
     * 在 Uint8Array 中查找 \r\n\r\n (头部结束标记)
     */
    const findHeaderEnd = (haystack: Uint8Array, startOffset: number = 0): number => {
      return findInBuffer(haystack, '\r\n\r\n', startOffset);
    };

    /**
     * 将 Uint8Array 的一部分转为文本（用于解析头部）
     */
    const bytesToText = (data: Uint8Array, start: number, end: number): string => {
      return textDecoder.decode(data.slice(start, end));
    };

    const readLoop = async () => {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;

        // 将新数据追加到缓冲区
        const newBuffer = new Uint8Array(buffer.length + value.length);
        newBuffer.set(buffer);
        newBuffer.set(value, buffer.length);
        buffer = newBuffer;

        // 循环处理缓冲区中所有完整的帧
        while (true) {
          // 查找帧边界标记
          const boundaryIdx = findInBuffer(buffer, boundaryMarker);
          if (boundaryIdx === -1) break;

          // 帧数据从边界标记之后开始
          const frameStart = boundaryIdx + boundaryMarker.length;

          // 查找下一个边界标记或结束标记
          const nextBoundaryIdx = findInBuffer(buffer, boundaryMarker, frameStart);
          const endBoundaryIdx = findInBuffer(buffer, boundaryEnd, frameStart);

          if (nextBoundaryIdx === -1 && endBoundaryIdx === -1) break;

          const frameEnd = nextBoundaryIdx !== -1 ? nextBoundaryIdx :
                           (endBoundaryIdx !== -1 ? endBoundaryIdx : -1);
          if (frameEnd === -1) break;

          // 提取这一帧的完整数据（头部 + JPEG 数据）
          const frameData = buffer.slice(frameStart, frameEnd);

          // 查找头部结束位置 (\r\n\r\n)
          const headerEndIdx = findHeaderEnd(frameData);
          if (headerEndIdx !== -1) {
            // 解析头部（ASCII 文本）
            const headerText = bytesToText(frameData, 0, headerEndIdx);

            // 提取 X-Timestamp（服务器 CLOCK_MONOTONIC，用于服务器端延迟显示）
            const tsMatch = headerText.match(/X-Timestamp:\s*(\d+)\.(\d+)/);
            // 提取 X-Timestamp-Realtime（服务器 CLOCK_REALTIME，用于端到端延迟计算）
            const rtMatch = headerText.match(/X-Timestamp-Realtime:\s*(\d+)/);

            if (tsMatch) {
              const serverSec = parseInt(tsMatch[1]);
              const serverNs = parseInt(tsMatch[2]);
              const serverTimeMs = serverSec * 1000 + serverNs / 1000000;

              // 将服务器 CLOCK_MONOTONIC 时间戳转换到 performance.now() 基准
              // 用于显示服务器端处理延迟（与 ESP 日志中的 latency 对应）
              const dateNow = Date.now();
              const perfNow = performance.now();
              const estimatedTimeOrigin = dateNow - perfNow;
              const serverTimePerf = serverTimeMs - estimatedTimeOrigin;
              const browserReceiveTime = performance.now();
              const latency = browserReceiveTime - serverTimePerf;

              if (latency > 0 && latency < 10000) {
                endToEndLatencyMs.value = Math.round(latency);
              }
            }

            // 使用 X-Timestamp-Realtime（wall clock）计算精确的端到端延迟
            if (rtMatch) {
              const serverWallClockMs = parseInt(rtMatch[1]);
              const browserWallClockMs = Date.now();

              // 端到端延迟 = 浏览器收到时间(wall clock) - 服务器发送时间(wall clock)
              const e2eLatency = browserWallClockMs - serverWallClockMs;

              if (e2eLatency > 0 && e2eLatency < 10000) { // 过滤无效值
                endToEndLatencyMs.value = Math.round(e2eLatency);

                // 根据延迟设置颜色
                if (e2eLatency < 50) latencyColor.value = 'green';
                else if (e2eLatency < 100) latencyColor.value = 'orange';
                else latencyColor.value = 'red';
              }
            }

            // 提取 JPEG 二进制数据（头部结束后的所有内容）
            const jpegData = frameData.slice(headerEndIdx + 4); // +4 for \r\n\r\n

            if (jpegData.length > 0) {
              // 创建 blob URL 用于显示
              const blob = new Blob([jpegData], { type: 'image/jpeg' });
              const blobUrl = URL.createObjectURL(blob);

              // 释放上一个 blob URL
              if (currentBlobUrl) {
                URL.revokeObjectURL(currentBlobUrl);
              }
              currentBlobUrl = blobUrl;

              // 更新 <v-img> 的 src
              imgSrc.value = blobUrl;
            }
          }

          // 从缓冲区中移除已处理的帧数据
          buffer = buffer.slice(frameEnd);
        }
      }
    };

    readLoop().catch(err => {
      if (err.name !== 'AbortError') {
        console.error('MJPEG stream error:', err);
        // 出错后3秒重连
        setTimeout(() => startMjpegStream(), 3000);
      }
    });

  } catch (err: any) {
    if (err.name !== 'AbortError') {
      console.error('MJPEG fetch error:', err);
      setTimeout(() => startMjpegStream(), 3000);
    }
  }
}

const reloadCameraSrc = (ms: number = 100) => {
  imgSrc.value = LOADING_IMAGE_SRC;

  if (reloadTimeoutId.value) {
    clearTimeout(reloadTimeoutId.value)
    reloadTimeoutId.value = null
  }

  reloadTimeoutId.value = setTimeout(() => {
    // 启动 MJPEG 流解析
    startMjpegStream();
    reloadTimeoutId.value = null;
  }, ms);
}

const captureFrame = () => {
  const url = `/api/capture_image?source=${camera.value.index}`;

  const link = document.createElement('a');
  link.href = url;
  link.download = `camera_${camera.value.index}_image.jpg`;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

const captureRawFrame = () => {
  const url = `/api/capture_binary?source=${camera.value.index}`;

  const link = document.createElement('a');
  link.href = url;
  link.download = `camera_${camera.value.index}_raw.bin`;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

const saveSettings = async () => {
  imgSrc.value = LOADING_IMAGE_SRC;
  settingsSaving.value = true;
  await fetch('/api/set_camera_config', {
    method: 'POST',
    body: JSON.stringify({
      index: camera.value.index,
      image_format: selectedImageFormatId.value,
      jpeg_quality: selectedQuality.value
    })
  }).then(res => {
    if (res.ok) {
      saveStatusSnackbar.value = true;
      saveStatusSnackbarText.value = "Settings saved";
    } else {
      saveStatusSnackbar.value = true;
      saveStatusSnackbarText.value = "Failed to save settings";
    }
  }).finally(() => {
    settingsSaving.value = false;
    settingsDialog.value = false;
    reloadCameraSrc();
  })
}

watch(realCameraUrl, (newUrl) => {
  if (imgSrc.value !== LOADING_IMAGE_SRC) {
    // 重新连接流
    startMjpegStream();
  }
})

watch(selectedImageFormatId, () => {
  if (selectedFormat.value?.quality) {
    selectedQuality.value = selectedFormat.value?.quality.default ?? selectedFormat.value?.quality.max ?? 90;
  }
})

onMounted(() => {
  reloadCameraSrc();
})

onUnmounted(() => {
  if (retryTimeoutId.value) {
    clearTimeout(retryTimeoutId.value)
    retryTimeoutId.value = null
  }
  if (reloadTimeoutId.value) {
    clearTimeout(reloadTimeoutId.value)
    reloadTimeoutId.value = null
  }
  // 取消 MJPEG 流
  if (abortController) {
    abortController.abort();
    abortController = null;
  }
})
</script>
