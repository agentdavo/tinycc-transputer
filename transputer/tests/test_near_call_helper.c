/* Second translation unit for test_near_call.c: an identical static twin(),
   called through a narrow slot, that ICF must not fold into the other one. */
static int twin(int x) { return x * 3 + 1; }

int twin_b(int x)
{
	return twin(x) + 100;
}
