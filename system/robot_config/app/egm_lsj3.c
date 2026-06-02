#include "HYYRobotInterface.h"
#include <stdio.h>  
#include <stdlib.h>  
#include <string.h>  
#include <unistd.h>  
#include <arpa/inet.h>  
#include <sys/socket.h>  
extern void recover_robot_move_state(int robot_index);

#define SERVER_IP "192.168.0.11"  
#define SERVER_PORT 6680  
#define BUFFER_SIZE sizeof(double) * 9
#define NumArray 5

char buffer[BUFFER_SIZE];  
double bufferF[NumArray][9] = {0};
int top = 0, topLast = 0;
int sockfd;  
struct sockaddr_in serverAddr;  
ssize_t receivedBytes;  
double camera_data[9]; 
int rec_init = 1;
robpose pinit, pinit2;

void* rec_function(void* arg) {
    robpose pcurr;

    while(1)
    {
        usleep(1000);
        if (top < (NumArray-2)) 
        {
            receivedBytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
            GetCurrentCartesian(NULL,NULL, &pcurr, 0);
            if (receivedBytes == -1) 
            {  
                perror("receive failed");  
            } 
            else 
            {  
                if(receivedBytes > 0)
                {
                    printf("Received %zd bytes of data from server.\n", receivedBytes);  
                    // 灏嗘帴鏀跺埌鐨勫瓧鑺傛暟鎹浆鎹负double鏁扮粍  
                    // for (size_t i = 0; i < receivedBytes / sizeof(double); i++) {  
                    //     memcpy(&camera_data[i], buffer + i * sizeof(double), sizeof(double));  
                    // }  

                    char delims[] = ",";
                    char *result = NULL;
                    result = strtok( buffer, delims );
                    int i = 0;  
                    while( result != NULL ) {
                        usleep(1000);
                        printf( "result is \"%s\"\n", result );
                        camera_data[i++] = atof(result); 
                        result = strtok( NULL, delims );
                    } 

                    printf("get:%f,%f,%f,%f,%f,%f,%f,%f,%f\n",camera_data[0],camera_data[1],camera_data[2],camera_data[3],camera_data[4],camera_data[5],camera_data[6],camera_data[7],camera_data[8]);

                    printf("pcurr:%f,%f,%f,%f,%f,%f\n",pcurr.xyz[0],pcurr.xyz[1],pcurr.xyz[2],pcurr.kps[0],pcurr.kps[1],pcurr.kps[2]);
                    
                    printf("top:%d\n",top);

                    bufferF[top][0] = camera_data[0] + pinit.xyz[0];
                    bufferF[top][1] = camera_data[1] + pinit.xyz[1];
                    bufferF[top][2] = camera_data[2] + pinit.xyz[2];
                    bufferF[top][3] = camera_data[3] + pinit2.xyz[0];
                    bufferF[top][4] = camera_data[4] + pinit2.xyz[1];
                    bufferF[top][5] = camera_data[5] + pinit2.xyz[2];
                    bufferF[top][6] = camera_data[6];
                    bufferF[top][7] = camera_data[7];
                    bufferF[top][8] = camera_data[8];
                    if(bufferF[top][6] < 0.7)
                        bufferF[top][6] = 0.7;
                    else if(bufferF[top][6] > 3.0)
                        bufferF[top][6] = 3.0;
                    if(bufferF[top][7] < 0.5)
                        bufferF[top][7] = 0;
                    else if(bufferF[top][7] > 0.5)
                        bufferF[top][7] = 1;
                    if(bufferF[top][8] < 0.5)
                        bufferF[top][8] = 0;
                    else if(bufferF[top][8] > 0.5)
                        bufferF[top][8] = 1;
                    topLast = top;
                    top++;
                    
                }
            }  
        }
        else
        {
            topLast = top;
            top--;
            
            for(int i = 0;i < top;i++)
            {
                bufferF[i][0] =  bufferF[i+1][0];
                bufferF[i][1] =  bufferF[i+1][1];
                bufferF[i][2] =  bufferF[i+1][2];
                bufferF[i][3] =  bufferF[i+1][3];
                bufferF[i][4] =  bufferF[i+1][4];
                bufferF[i][5] =  bufferF[i+1][5];
                bufferF[i][6] =  bufferF[i+1][6];
                bufferF[i][7] =  bufferF[i+1][7];
                bufferF[i][8] =  bufferF[i+1][8];
            }
            //sleep(1000);
        }
    }
    
    return NULL; 
}


 void* rec_function2(void* arg) {
    
    while (robot_ok())//寰幆鎿嶄綔
    {
        //printf("robot_ok!\n");
        //鑾峰彇杩愬姩鐩爣鏁版嵁

        if(top == 0)
        {
            usleep(1000);
            continue;
        }
        
        // printf("gripperR: %f\n",bufferF[0][8]);
        SETSPEEDTIME(vt,bufferF[0][6]);
        SETPOSE(pc2,bufferF[0][3],bufferF[0][4],bufferF[0][5],pinit2.kps[0],pinit2.kps[1],pinit2.kps[2]);//璁剧疆鐩爣鏁版嵁
        

        if(top == 1 && topLast > top)
            multi_moveL(pc2,vt,NULL,NULL,NULL,1);
        else
            multi_moveL(pc2,vt,NULL,NULL,NULL,1);

        // topLast = top;
        // top--;
        // for(int i = 0;i < top;i++)
        // {
        //     bufferF[i][0] =  bufferF[i+1][0];
        //     bufferF[i][1] =  bufferF[i+1][1];
        //     bufferF[i][2] =  bufferF[i+1][2];
        //     bufferF[i][3] =  bufferF[i+1][3];
        //     bufferF[i][4] =  bufferF[i+1][4];
        //     bufferF[i][5] =  bufferF[i+1][5];
        //     bufferF[i][6] =  bufferF[i+1][6];
        //     bufferF[i][7] =  bufferF[i+1][7];
        //     bufferF[i][8] =  bufferF[i+1][8];
        // }
        
        usleep(1000);
        
    }
    
    return NULL; 
}

void* rec_function_init(void* arg) {
    SETJOINT7(path1_initR, 1.998, 1.000, -0.269, 1.239, 0.223, 0.733, 0.5742);
    SETSPEED(v50,0.08,0.08);
    multi_moveA(path1_initR,v50,NULL,NULL,NULL,1);
    printf("The right arm is initialized!\n");
    rec_init = 0;
    GetCurrentCartesian(NULL,NULL, &pinit2, 1);
}

void* rec_function_RG(void* arg) {
    double GripLastR = 1.0;
    int flag = 0;
    while(robot_ok())
    {
        /****夹爪控制****/
        if(bufferF[0][7] < 0.5 && GripLastR > 0.5)      //close
        {
            SetDo(17,1);
            Rsleep(50);
            SetDo(17,0);
            printf("GripR Close\n");
            flag = 0;
            GripLastR = bufferF[0][7];  
        }
        else if(bufferF[0][7] > 0.5 && GripLastR < 0.5)      //open
        {
            SetDo(16,1);
            Rsleep(50);
            SetDo(16,0);
            printf("GripR Open\n");
            flag = 0;
            GripLastR = bufferF[0][7];  
        }
        else
        {
            usleep(1000);
            if(flag == 0)
            {
                flag = 1;
                printf("GripR:%f, GripLastR:%f\n", bufferF[0][7], GripLastR);
            }
        }

        // if(bufferF[0][4] == 1.0 && GripLastL != 1.0)
        // {
        //     ret=ControlGrip("grip", 0);         //0  全 煽 
        //     //Rsleep(2000);//2s
        // }
        // else if(bufferF[0][4] == 0.0 && GripLastL != 0.0)
        // {
        //     ret=ControlGrip("grip", 1);         //1  全 薪 
        //     //Rsleep(2000);//2s
        // }
    }
    
}

void* rec_function_LG(void* arg) {
    double GripLastL = 1.0;
    int flag = 0;
    while(robot_ok())
    {
        /****夹爪控制****/
        if(bufferF[0][8] < 0.5 && GripLastL > 0.5)      //close
        {
            SetDo(1,1);
            Rsleep(50);
            SetDo(1,0);
            printf("GripL Close\n");
            flag = 0;
            GripLastL = bufferF[0][8];   
        }
        else if(bufferF[0][8] > 0.5 && GripLastL < 0.5)      //open
        {
            SetDo(0,1);
            Rsleep(50);
            SetDo(0,0);
            printf("GripL Open\n");
            flag = 0;
            GripLastL = bufferF[0][8];   
        }
        else
        {
            usleep(1000);
            if(flag == 0)
            {
                flag = 1;
                printf("GripL:%f, GripLastL:%f\n", bufferF[0][8], GripLastL);
            }
        }
    }
}

int MainModule()
{
    SETJOINT7(path1_initL, -1.117, 0.8, -0.3142, 1.326, 0.2665, 0.7505, 0.5760);
    IMPORTWOBJ(wobj0);
    SETZONE(zone,0.1);

    SETSPEED(v50,0.08,0.08);//寮哄埗鎭㈠閫熷害
    //-------------------璁剧疆鍏佽绌洪棿---------------------------
	int ret=0;
    
    // ret=CreateGrip("grip");
    printf("00000\n");
    ThreadCreat(rec_function_init, NULL, "rec_function_init", 1);
    multi_moveA(path1_initL,v50,NULL,NULL,NULL,0);
    printf("The left arm is initialized!\n");
    
    while(rec_init == 1) 
    {
        usleep(1000);
    }
    printf("11111\n");
    // ret=ControlGrip("grip", 0);         //0  全 煽 
    Rsleep(2000);
    
    GetCurrentCartesian(NULL,NULL, &pinit, 0);
    
    // RESTART:
    // OnSpatialConstraint();//寮€鍚瑳鍗″皵绌洪棿绾︽潫妫€ ?

    //struct sockaddr_in server_addr;

    // 鍒涘缓socket  
    sockfd = socket(AF_INET, SOCK_STREAM, 0);  
    if (sockfd == -1) {  
        perror("socket creation failed");  
        exit(EXIT_FAILURE);  
    }  
  
    // 璁剧疆鏈嶅姟鍣ㄥ湴鍧€淇℃伅  
    memset(&serverAddr, 0, sizeof(serverAddr));  
    serverAddr.sin_family = AF_INET;  
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);  
    serverAddr.sin_port = htons(SERVER_PORT);  
  
    // 杩炴帴鍒版湇鍔″櫒  
    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {  
        perror("connection failed");  
        close(sockfd);  
        exit(EXIT_FAILURE);  
    }  

    printf("Connected to server.\n");  
    ThreadCreat(rec_function, NULL, "rec_function", 1);
    ThreadCreat(rec_function2, NULL, "rec_function2", 1);
    ThreadCreat(rec_function_LG, NULL, "rec_function_LG", 1);
    ThreadCreat(rec_function_RG, NULL, "rec_function_RG", 1);
    while (robot_ok())//寰幆鎿嶄綔
    {
        //printf("robot_ok!\n");
        //鑾峰彇杩愬姩鐩爣鏁版嵁

        if(top == 0)
        {
            usleep(1000);
            continue;
        }
        
        //printf("time: %f\n",bufferF[0][6]);
        //printf("gripperL: %f\n",bufferF[0][7]);
        
        SETSPEEDTIME(vt,bufferF[0][6]);
        SETPOSE(pc,bufferF[0][0],bufferF[0][1],bufferF[0][2],pinit.kps[0],pinit.kps[1],pinit.kps[2]);//璁剧疆鐩爣鏁版嵁

        if(top == 1 && topLast > top)
            multi_moveL(pc,vt,NULL,NULL,NULL,0);
        else
            multi_moveL(pc,vt,NULL,NULL,NULL,0);

        topLast = top;
        top--;
        for(int i = 0;i < top;i++)
        {
            bufferF[i][0] =  bufferF[i+1][0];
            bufferF[i][1] =  bufferF[i+1][1];
            bufferF[i][2] =  bufferF[i+1][2];
            bufferF[i][3] =  bufferF[i+1][3];
            bufferF[i][4] =  bufferF[i+1][4];
            bufferF[i][5] =  bufferF[i+1][5];
            bufferF[i][6] =  bufferF[i+1][6];
            bufferF[i][7] =  bufferF[i+1][7];
            bufferF[i][8] =  bufferF[i+1][8];
        }

        usleep(1000);
    }

    ThreadDataFree("rec_function_init");
    ThreadDataFree("rec_function");
    ThreadDataFree("rec_function2");
    ThreadDataFree("rec_function_LG");
    ThreadDataFree("rec_function_RG");
    close(sockfd);  
	DeleteAllSpatialConstraint();

    // ret=SFCEnd("SFCtest");
    // printf("SFCEnd,ret=%d\n",ret);
	return 0;
}
