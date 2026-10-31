/* test_boot.S calls test_main; CoreMark provides main. */
extern int main(void);

void test_main(void)
{
	main();
}
