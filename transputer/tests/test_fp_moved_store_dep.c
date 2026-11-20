/* A moved FP producer must retain a store read by an assignment's copy.
 * Expected: SCOPY-DEP PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)
static float ff(float a,float b)
{ float x;float y=(x=a+b)*(a-b);return x+y; }
static double dd(double a,double b)
{ double x;double y=(x=a+b)*(a-b);return x+y; }
void test_main(void)
{
    int ok=ff(3,1)==12 && ff(5,1)==30 && ff(7,2)==54 &&
           dd(3,1)==12 && dd(5,1)==30 && dd(7,2)==54;
    const char *s=ok?"COPY-DEP PASS\n":"COPY-DEP FAIL\n";
    while(*s)UART=(unsigned char)*s++;
}
