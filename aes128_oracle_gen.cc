#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include <iostream>
#include <fstream>

#include <NTL/mat_GF2.h>

#include "aes128_private.h"

namespace {

template<typename T>
inline NTL::vec_GF2 from_scalar(T in);

template<>
inline NTL::vec_GF2 from_scalar(uint8_t in) {
  NTL::vec_GF2 result;
  result.SetLength(8);
  for (int i = 0; i < 8; i++) {
    result[7 - i] = ((in >> i) & 1);
  }
  return result;
}

template<>
inline NTL::vec_GF2 from_scalar(uint32_t in) {
  NTL::vec_GF2 result;
  result.SetLength(32);
  for (int i = 0; i < 32; i++) {
    result[31 - i] = ((in >> i) & 1);
  }
  return result;
}

template<typename T>
inline T to_scalar(const NTL::vec_GF2& in);

template<>
inline uint8_t to_scalar(const NTL::vec_GF2& in) {
  uint8_t result = 0;
  for (int i = 0; i < 2; i++) {
    long i0 = NTL::rep(in[i*4+0]), i1 = NTL::rep(in[i*4+1]),
         i2 = NTL::rep(in[i*4+2]), i3 = NTL::rep(in[i*4+3]);
    result = (result << 4) | (i0 << 3) | (i1 << 2) | (i2 << 1) | (i3 << 0);
  }
  return result;
}

template<>
inline uint32_t to_scalar(const NTL::vec_GF2& in) {
  uint32_t result = 0;
  for (int i = 0; i < 8; i++) {
    long i0 = NTL::rep(in[i*4+0]), i1 = NTL::rep(in[i*4+1]),
         i2 = NTL::rep(in[i*4+2]), i3 = NTL::rep(in[i*4+3]);
    result = (result << 4) | (i0 << 3) | (i1 << 2) | (i2 << 1) | (i3 << 0);
  }
  return result;
}

template<typename T>
inline T mul(const NTL::mat_GF2& mat, T x) {
  return to_scalar<T>(mat * from_scalar<T>(x));
}

NTL::mat_GF2 GenerateGF2RandomMatrix(int dimension) {
  NTL::mat_GF2 mat(NTL::INIT_SIZE, dimension, dimension);
  for (int i = 0; i < dimension; i++) {
    for (int j = 0; j < dimension; j++) {
      mat[i][j] = NTL::random_GF2();
    }
  }
  return mat;
}

NTL::mat_GF2 GenerateRandomGF2InvertibleMatrix(int dimension) {
  for (;;) {
    NTL::mat_GF2 result = GenerateGF2RandomMatrix(dimension);
    if (NTL::determinant(result) != 0)
      return result;
  }
}

void Generate8x8MixingBijections(NTL::mat_GF2 L[9][16]) {
  for (int r = 0; r < 9; r++) {
    for (int i = 0; i < 16; i++) {
      L[r][i] = GenerateRandomGF2InvertibleMatrix(8);
    }
  }
}

void Generate32x32MixingBijections(NTL::mat_GF2 MB[9][4]) {
  for (int r = 0; r < 9; r++) {
    for (int i = 0; i < 4; i++) {
      MB[r][i] = GenerateRandomGF2InvertibleMatrix(32);
    }
  }
}

// Calculate the T-boxes, which is a combination of the AddRoundKeyAfterShift
// and the SubBytes functions.
void CalculateTboxes(const uint32_t roundKey[44],
    uint8_t Tboxes[10][16][256]) {
  for (int r = 0; r < 10; r++) {
    for (int x = 0; x < 256; x++) {
      uint8_t state[16] = {
        (uint8_t)x, (uint8_t)x, (uint8_t)x, (uint8_t)x,
        (uint8_t)x, (uint8_t)x, (uint8_t)x, (uint8_t)x,
        (uint8_t)x, (uint8_t)x, (uint8_t)x, (uint8_t)x,
        (uint8_t)x, (uint8_t)x, (uint8_t)x, (uint8_t)x
      };
      AddRoundKeyAfterShift(state, &roundKey[r*4]);
      SubBytes(state);
      if (r == 9) {
        AddRoundKey(state, &roundKey[40]);
      }
      for (int i = 0; i < 16; i++) {
        Tboxes[r][i][x] = state[i];
      }
    }
  }
}

void CalculateTy(uint8_t Ty[4][256][4]) {
  for (int x = 0; x < 256; x++) {
    Ty[0][x][0] = gf_mul[x][0];
    Ty[0][x][1] = gf_mul[x][1];
    Ty[0][x][2] = x;
    Ty[0][x][3] = x;

    Ty[1][x][0] = x;
    Ty[1][x][1] = gf_mul[x][0];
    Ty[1][x][2] = gf_mul[x][1];
    Ty[1][x][3] = x;

    Ty[2][x][0] = x;
    Ty[2][x][1] = x;
    Ty[2][x][2] = gf_mul[x][0];
    Ty[2][x][3] = gf_mul[x][1];

    Ty[3][x][0] = gf_mul[x][1];
    Ty[3][x][1] = x;
    Ty[3][x][2] = x;
    Ty[3][x][3] = gf_mul[x][0];
  }
}

void CalculateTyBoxes(uint32_t roundKey[44],
    uint32_t Tyboxes[9][16][256], uint8_t Tboxes10[16][256],
    uint32_t MBL[9][16][256], bool enableL, bool enableMB) {
  uint8_t Tboxes[10][16][256];
  uint8_t Ty[4][256][4];

  CalculateTboxes(roundKey, Tboxes);
  CalculateTy(Ty);

  for (int r = 0; r < 9; r++) {
    for (int x = 0; x < 256; x++) {
      for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
          uint32_t v0 = Ty[0][Tboxes[r][j*4 + i][x]][i],
                   v1 = Ty[1][Tboxes[r][j*4 + i][x]][i],
                   v2 = Ty[2][Tboxes[r][j*4 + i][x]][i],
                   v3 = Ty[3][Tboxes[r][j*4 + i][x]][i];
          Tyboxes[r][j*4 + i][x] = (v0 << 24) | (v1 << 16) | (v2 << 8) | v3;
          MBL[r][j*4 + i][x] = x << ((3 - i) << 3);
        }
      }
    }
  }

  for (int x = 0; x < 256; x++) {
    for (int i = 0; i < 16; i++) {
      Tboxes10[i][x] = Tboxes[9][i][x];
    }
  }

  if (enableMB) {
    NTL::mat_GF2 MB[9][4];
    Generate32x32MixingBijections(MB);

    // When applying MB and inv(MB), the operation is quite easy; there is no
    // need to safeguard the existing table, as it is a simple substitution. 
    for (int r = 0; r < 9; r++) {
      for (int x = 0; x < 256; x++) {
        for (int i = 0; i < 4; i++) {
          Tyboxes[r][i][x] = mul<uint32_t>(MB[r][i >> 2], Tyboxes[r][i][x]);
          MBL[r][i][x] = mul<uint32_t>(NTL::inv(MB[r][i >> 2]), MBL[r][i][x]);
        }
      }
    }
  }

  if (enableL) {
    NTL::mat_GF2 L[9][16];
    Generate8x8MixingBijections(L);

    // When applying L and inv(L), things get a little tricky. As it involves
    // non-linear substitutions, the original table has to be copied before
    // being updated.
    for (int r = 0; r < 9; r++) {
      
      if (r > 0) {
        // Rounds 1 to 8 are reversed here.
        for (int i = 0; i < 16; i++) {
          uint32_t oldTyboxes[256];
          for (int x = 0; x < 256; x++)
            oldTyboxes[x] = Tyboxes[r][i][x];
          for (int x = 0; x < 256; x++)
            Tyboxes[r][i][x] = oldTyboxes[mul<uint8_t>(NTL::inv(L[r-1][i]), x)];
        }
      }
  
      // Apply the L transformation at each round.
      for (int j = 0; j < 4; ++j) {
        for (int x = 0; x < 256; x++) {
          uint32_t out0 = MBL[r][j*4 + 0][x];
          uint32_t out1 = MBL[r][j*4 + 1][x];
          uint32_t out2 = MBL[r][j*4 + 2][x];
          uint32_t out3 = MBL[r][j*4 + 3][x];
  
          MBL[r][j*4 + 0][x] = (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 0]], out0 >> 24) << 24)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 1]], out0 >> 16) << 16)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 2]], out0 >>  8) <<  8)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 3]], out0 >>  0) <<  0);
  
          MBL[r][j*4 + 1][x] = (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 0]], out1 >> 24) << 24)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 1]], out1 >> 16) << 16)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 2]], out1 >>  8) <<  8)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 3]], out1 >>  0) <<  0);
  
          MBL[r][j*4 + 2][x] = (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 0]], out2 >> 24) << 24)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 1]], out2 >> 16) << 16)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 2]], out2 >>  8) <<  8)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 3]], out2 >>  0) <<  0);
  
          MBL[r][j*4 + 3][x] = (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 0]], out3 >> 24) << 24)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 1]], out3 >> 16) << 16)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 2]], out3 >>  8) <<  8)
                             | (mul<uint8_t>(L[r][InvShiftRowsTab[j*4 + 3]], out3 >>  0) <<  0);
        }
      }
    }
  
    // The last and final round 9 is reversed here.
    for (int i = 0; i < 16; i++) {
      uint8_t oldTboxes10[256];
      for (int x = 0; x < 256; x++)
        oldTboxes10[x] = Tboxes10[i][x];
      for (int x = 0; x < 256; x++)
        Tboxes10[i][x] = oldTboxes10[mul<uint8_t>(NTL::inv(L[8][i]), x)];
    }
  }
}

void GenerateXorTable(FILE* out) {
  uint8_t Xor[9][96][16][16];
  for (int r = 0; r < 9; r++)
    for (int n = 0; n < 96; n++)
      for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
          Xor[r][n][i][j] = i ^ j;

  fprintf(out, "constexpr uint8_t Xor[9][96][16][16] = {\n");
  for (int r = 0; r < 9; r++) {
    fprintf(out, "  {\n");
    for (int n = 0; n < 96; n++) {
      fprintf(out, "    {\n");
      for (int i = 0; i < 16; i++) {
        fprintf(out, "      { ");
        for (int j = 0; j < 16; j++)
          fprintf(out, "0x%02x, ", Xor[r][n][i][j]);
        fprintf(out, "},\n");
      }
      fprintf(out, "    },\n");
    }
    fprintf(out, "  },\n");
  }
  fprintf(out, "};\n\n");
}

void GenerateEncryptingTables(FILE* out, uint32_t roundKey[44]) {
  uint32_t Tyboxes[9][16][256];
  uint8_t Tboxes10[16][256];
  uint32_t MBL[9][16][256];

  CalculateTyBoxes(roundKey, Tyboxes, Tboxes10, MBL, true, true);

  fprintf(out, "constexpr uint32_t Tyboxes[9][16][256] = {\n");
  for (int r = 0; r < 9; r++) {
    fprintf(out, "  {\n");
    for (int i = 0; i < 16; i++) {
      fprintf(out, "    {\n");
      for (int x = 0; x < 256; x++) {
        if ((x % 8) == 0) {
          fprintf(out, "      ");
        }
        fprintf(out, "0x%08x,", Tyboxes[r][i][x]);
        if (x > 0 && (x % 8) == 7) {
          fprintf(out, "\n");
        } else {
          fprintf(out, " ");
        }
      }
      fprintf(out, "    },\n");
    }
    fprintf(out, "  },\n");
  }
  fprintf(out, "};\n\n");

  fprintf(out, "constexpr uint8_t Tboxes10[16][256] = {\n");
  for (int i = 0; i < 16; i++) {
    fprintf(out, "  {\n");
    for (int x = 0; x < 256; x++) {
      if (x % 16 == 0) {
        fprintf(out, "    ");
      }
      fprintf(out, "0x%02x, ", Tboxes10[i][x]);
      if (x % 16 == 15) {
        fprintf(out, "\n");
      }
    }
    fprintf(out, "  },\n");
  }
  fprintf(out, "};\n\n");

  fprintf(out, "constexpr uint32_t MBL[9][16][256] = {\n");
  for (int r = 0; r < 9; r++) {
    fprintf(out, "  {\n");
    for (int i = 0; i < 16; i++) {
      fprintf(out, "    {\n");
      for (int x = 0; x < 256; x++) {
        if ((x % 8) == 0) {
          fprintf(out, "      ");
        }
        fprintf(out, "0x%08x,", MBL[r][i][x]);
        if (x > 0 && (x % 8) == 7) {
          fprintf(out, "\n");
        } else {
          fprintf(out, " ");
        }
      }
      fprintf(out, "    },\n");
    }
    fprintf(out, "  },\n");
  }
  fprintf(out, "};\n\n");
}

void GenerateTables(const uint8_t key[16]) {
  uint32_t roundKey[44];
  FILE* out = fopen("aes128_oracle_tables.cc", "w");

  ExpandKeys(key, roundKey);

  fprintf(out, "// This file is generated, do not edit.\n\n");
  fprintf(out, "namespace {\n\n");

  GenerateXorTable(out);
  GenerateEncryptingTables(out, roundKey);

  fprintf(out, "}  // namespace");

  fflush(out);
  fclose(out);
}

void err_quit(const char *fmt, ...) {
  va_list ap;
  char buf[1024];

  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);
  strcat(buf, "\n");
  fputs(buf, stderr);
  fflush(stderr);
  va_end(ap);

  exit(1);
}

void read_key(const char *in, uint8_t key[16]) {
  if (strlen(in) != 32)
    err_quit("Invalid key (should be a valid 128-bits hexadecimal string)");
  for (int i = 0; i < 16; i++) {
    sscanf(in + i * 2, "%2hhx", key + i);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  uint8_t key[16];

  if (argc != 2)
    err_quit("Syntax: aes128_oracle_gen <AES-128-hex-key>");

  read_key(argv[1], key);
  GenerateTables(key);
  return 0;
}
