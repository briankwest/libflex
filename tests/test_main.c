#include <stdio.h>

int test_pass = 0;
int test_fail = 0;

extern void test_bch(void);
extern void test_interleave(void);
extern void test_sync(void);
extern void test_fiw(void);
extern void test_biw(void);
extern void test_numeric(void);
extern void test_alpha(void);
extern void test_binary(void);
extern void test_codeword(void);
extern void test_phase(void);
extern void test_encoder(void);
extern void test_decoder(void);
extern void test_roundtrip(void);
extern void test_modem(void);

int main(void)
{
	printf("libflex test suite\n\n");

	test_bch();
	test_interleave();
	test_sync();
	test_fiw();
	test_biw();
	test_numeric();
	test_alpha();
	test_binary();
	test_codeword();
	test_phase();
	test_encoder();
	test_decoder();
	test_roundtrip();
	test_modem();

	printf("\n%d passed, %d failed\n", test_pass, test_fail);
	return test_fail > 0 ? 1 : 0;
}
