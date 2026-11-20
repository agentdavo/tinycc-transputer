/* A pair straddling outgoing and movable workspace must stay contiguous.
 * Expected: SADD PASS\nTURB PASS\nH
 */
#define UART (*(volatile unsigned*)0x10000000u)
struct v { double x,y,z; };
static void add(struct v *out,struct v *in,double scale)
{out->x+=in->x*scale;out->y+=in->y*scale;out->z+=in->z*scale;}
static void sample(struct v *v,double x,double y,double z)
{v->x=x+y;v->y=y+z;v->z=z+x;}
static void turbulence(struct v *out,double x,double y,double z,int n)
{int i;double scale;struct v v;out->x=out->y=out->z=0;
 for(i=0,scale=1;i<n;i++,scale*=0.5){sample(&v,x/scale,y/scale,z/scale);out->x+=v.x*scale;out->y+=v.y*scale;out->z+=v.z*scale;}}
void test_main(void)
{struct v a={10,20,30},b={1,2,3};int ok;add(&a,&b,0.5);ok=a.x==10.5&&a.y==21&&a.z==31.5;
 {const char*s=ok?"ADD PASS\n":"ADD FAIL\n";while(*s)UART=*s++;}
 turbulence(&a,1,2,4,4);ok=a.x==12&&a.y==24&&a.z==20;
 {const char*s=ok?"TURB PASS\n":"TURB FAIL\n";while(*s)UART=*s++;}}
