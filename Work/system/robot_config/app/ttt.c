
#include "HYYRobotInterface.h"
int MainModule()
{
            robjoint jx;
            speed sp;
            getspeed("v50",&sp);
            double data[6]={0,0,0,0,1,0};
            init_robjoint(&jx, data, 6);
            moveA(&jx,&sp,NULL,NULL,NULL);
            robpose p22;
            robpose p22x;
            getrobpose("p22",&p22);
            tool tool12;
            gettool("tool12",&tool12);
            p22x=Offs(&p22, 466, -163, 97, 0, 0, 0);
            moveL(&p22x,&sp,NULL,&tool12,NULL);
    return 0;
}
