
#include "HYYRobotInterface.h"
int MainModule()
{
	int i=0;
	const char *sName="calib";
	if (0!=ClientCreate("192.168.0.55", 8080, sName))
	{
		return 0;
	}
	byte buf[1024];
	int buf_n=0;
	int dof= robot_getDOF(0);
	char joint_name[10];
	double data[10];
	IMPORTSPEED(v500);
	uint16_t offset=0;
	int res=0;
	while (robot_ok())
	{
		buf_n=SocketRecvByteI(buf, 1024,sName);
		if (buf_n>0)
		{
			if (IsProtocolRight(buf, (uint16_t)buf_n))
			{
				char value[1024];
				GetProtocolDataString(buf, "function", value);
				if (0==strcmp("start",value))
				{
					for (i=0;i<dof;i++)
					{
						sprintf(joint_name,"%d",i);
						if (0!=GetProtocolDataDouble(buf, joint_name, &(data[i])))
						{
							res=1;
							break;
						}	
					}
					if (res)
					{
						memset(buf,0,sizeof(buf));
						offset=SetProtocolDataString(buf,0, "result", "failure");
					}
					else
					{
						SETJOINT6(target,data[0],data[1],data[2],data[3],data[4],data[5]);
						MoveA(target,v500,NULL,NULL,NULL);
						Rsleep(2000);
						memset(buf,0,sizeof(buf));
						if (robot_ok())
						{
							offset=SetProtocolDataString(buf,0, "result", "finish");
						}
						else
						{
							offset=SetProtocolDataString(buf,0, "result", "failure");
						}
					}
					SocketSendByteI(buf, offset, sName);
				}
			}
			
		}
		



	}



	SocketClose(sName);
	return 0;
}
