#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xff84034a, "module_layout" },
	{ 0xdd8657e8, "param_ops_int" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0xbab7d681, "class_destroy" },
	{ 0x25d638cd, "device_destroy" },
	{ 0x2ddb5d72, "pci_unregister_driver" },
	{ 0x7cfb8e3c, "device_create" },
	{ 0x67028218, "__class_create" },
	{ 0xc99f5fb7, "__pci_register_driver" },
	{ 0x81f395a7, "__register_chrdev" },
	{ 0x6a5cb5ee, "__get_free_pages" },
	{ 0x46cf10eb, "cachemode2protval" },
	{ 0xa92ec74, "boot_cpu_data" },
	{ 0xd1837b2e, "remap_pfn_range" },
	{ 0xfb578fc5, "memset" },
	{ 0xc968cd0d, "dma_alloc_attrs" },
	{ 0x67f4200e, "pci_save_state" },
	{ 0x4fa71882, "pci_enable_pcie_error_reporting" },
	{ 0xca1890b2, "pci_enable_device_mem" },
	{ 0xd67a4fc4, "try_module_get" },
	{ 0x1814716b, "kmem_cache_alloc_trace" },
	{ 0x7c1840da, "kmalloc_caches" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0xfcec0987, "enable_irq" },
	{ 0x1000e51, "schedule" },
	{ 0x1a3a5952, "prepare_to_wait" },
	{ 0x50bfc6af, "finish_wait" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0xad73041f, "autoremove_wake_function" },
	{ 0xce8e7988, "current_task" },
	{ 0x555ca567, "pci_enable_msi" },
	{ 0x4fb31390, "pci_set_master" },
	{ 0xa8a3b542, "dma_set_coherent_mask" },
	{ 0xa35b4550, "dma_set_mask" },
	{ 0x1dc8d9de, "pci_try_set_mwi" },
	{ 0x87a9ab54, "pci_request_regions" },
	{ 0x7f2695a3, "pci_enable_device" },
	{ 0xffb50d79, "pci_get_domain_bus_and_slot" },
	{ 0xb8e7ce2c, "__put_user_8" },
	{ 0xcba7ee4d, "pci_get_device" },
	{ 0xb2fd5ceb, "__put_user_4" },
	{ 0x6729d3df, "__get_user_4" },
	{ 0xc08eaa90, "irq_get_irq_data" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0x5a2daeea, "__init_waitqueue_head" },
	{ 0x522f6953, "pci_clear_master" },
	{ 0x36842f0b, "module_put" },
	{ 0x47b1fc86, "mutex_unlock" },
	{ 0x90868e5f, "mutex_lock" },
	{ 0xc1514a3b, "free_irq" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x27bbf221, "disable_irq_nosync" },
	{ 0xf1c1d1c, "__wake_up" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xd589fa57, "pci_disable_device" },
	{ 0x45ac5b3a, "pci_disable_pcie_error_reporting" },
	{ 0xa22ea54c, "pci_release_regions" },
	{ 0x16978e48, "pci_disable_msi" },
	{ 0x92997ed8, "_printk" },
	{ 0x37a0cba, "kfree" },
	{ 0x645fe8bc, "dma_free_attrs" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x4302d0eb, "free_pages" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x4c9d28b0, "phys_base" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("pci:v00008086d*sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010ECd*sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000015ECd*sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "48BFA3E6C91E183F3C3B734");
