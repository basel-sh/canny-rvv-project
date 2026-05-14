#include <stdio.h>
#include <riscv_vector.h>

int main() {
    int32_t a[4] = {1, 2, 3, 4};
    int32_t b[4] = {10, 20, 30, 40};
    int32_t result[4];

    size_t vl = __riscv_vsetvl_e32m1(4);

    vint32m1_t va = __riscv_vle32_v_i32m1(a, vl);
    vint32m1_t vb = __riscv_vle32_v_i32m1(b, vl);

    vint32m1_t vc = __riscv_vadd_vv_i32m1(va, vb, vl);

    __riscv_vse32_v_i32m1(result, vc, vl);

    printf("Result: ");

    for (int i = 0; i < 4; i++) {
        printf("%d ", result[i]);
    }

    printf("\n");

    return 0;
}