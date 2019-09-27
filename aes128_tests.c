#include "aunit.h"

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "aes128.h"
#include "aes128_oracle.h"

au_main

{
/* This is the embedded key, check the Makefile:
  const uint8_t key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
  };
*/
  const uint8_t plain[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
  };
  const uint8_t cipher[16] = {
    0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20,
    0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a
  };
  const uint8_t iv[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
  };

  uint8_t output[16];

  aes128_oracle_encrypt_cfb(iv, plain, 16, output);
  au_eq("Set 3, vector#  1/enc/tab", memcmp(output, cipher, sizeof(cipher)), 0);
  aes128_oracle_decrypt_cfb(iv, cipher, 16, output);
  au_eq("Set 3, vector#  1/dec/tab", memcmp(output, plain, sizeof(plain)), 0);
}

au_endmain
