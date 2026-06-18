// #include <riscv_vector.h>
// #include <cstdint>
// #include <cstdio>
// #include <cstring>

// static void report(const char* name, bool ok) {
//     printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
// }

// static bool test_vsetvl() {
//     size_t vl = __riscv_vsetvl_e32m1(64);
//     return (vl > 0 && vl <= 64);
// }

// static bool test_vadd_i32() {
//     const int N = 16;
//     int32_t a[N], b[N], c[N], expected[N];
//     for (int i = 0; i < N; i++) { a[i] = i+1; b[i] = 10; expected[i] = a[i]+b[i]; }
//     int32_t *pa=a, *pb=b, *pc=c; size_t n=N;
//     while (n > 0) {
//         size_t vl = __riscv_vsetvl_e32m1(n);
//         auto va = __riscv_vle32_v_i32m1(pa, vl);
//         auto vb = __riscv_vle32_v_i32m1(pb, vl);
//         __riscv_vse32_v_i32m1(pc, __riscv_vadd_vv_i32m1(va, vb, vl), vl);
//         pa+=vl; pb+=vl; pc+=vl; n-=vl;
//     }
//     return memcmp(c, expected, N*sizeof(int32_t)) == 0;
// }

// static bool test_vfadd_f32() {
//     const int N = 8;
//     float a[N], b[N], c[N];
//     for (int i = 0; i < N; i++) { a[i]=(i+1)*1.5f; b[i]=100.0f; }
//     float *pa=a, *pb=b, *pc=c; size_t n=N;
//     while (n > 0) {
//         size_t vl = __riscv_vsetvl_e32m1(n);
//         auto va = __riscv_vle32_v_f32m1(pa, vl);
//         auto vb = __riscv_vle32_v_f32m1(pb, vl);
//         __riscv_vse32_v_f32m1(pc, __riscv_vfadd_vv_f32m1(va, vb, vl), vl);
//         pa+=vl; pb+=vl; pc+=vl; n-=vl;
//     }
//     for (int i = 0; i < N; i++) if (c[i] != a[i]+b[i]) return false;
//     return true;
// }

// int main() {
//     printf("\n============================================\n");
//     printf("  RISC-V RVV Environment Verification\n");
//     printf("============================================\n\n");
//     int passed=0, total=0;
//     auto run = [&](const char* name, bool ok) {
//         report(name, ok); if (ok) passed++; total++;
//     };
//     run("test_vsetvl",   test_vsetvl());
//     run("test_vadd_i32", test_vadd_i32());
//     run("test_vfadd_f32",test_vfadd_f32());
//     printf("\n--------------------------------------------\n");
//     printf("  Result: %d / %d tests passed\n", passed, total);
//     printf("--------------------------------------------\n\n");
//     if (passed == total) { printf("  RVV environment is ready!\n\n"); return 0; }
//     printf("  Some tests failed.\n\n"); return 1;
// }
