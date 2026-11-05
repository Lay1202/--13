#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define MAXKEY        1000000
#define SUPP_TRIALS   100
#define TIME_REPEAT   300
#define WARMUP_RUNS   5
#define DATA_SEED     20240601ULL
#define PIVOT_SEED    987654321ULL

enum { ALG_MERGE = 0, ALG_QUICK_FIRST = 1, ALG_QUICK_RANDOM = 2, ALG_QUICK_MEDIAN3 = 3 };
enum { PIVOT_FIRST = 0, PIVOT_RANDOM = 1, PIVOT_MEDIAN3 = 2 };

#define NALG 4

static const char *ALG_LABEL[NALG] = {
    "Merge Sort",
    "Quick (First)",
    "Quick (Random)",
    "Quick (Median3)"
};

static const int SIZES[] = {1000, 2000, 4000, 8000, 16000, 20000};
#define NSIZES ((int)(sizeof(SIZES) / sizeof(SIZES[0])))

static long long g_comp = 0;
static long long g_heap = 0;
static int g_depth = 0;
static int g_maxdepth = 0;

static unsigned long long data_state = DATA_SEED;
static unsigned long long pivot_state = PIVOT_SEED;

static unsigned long long xorshift(unsigned long long *s) {
    *s ^= *s >> 12;
    *s ^= *s << 25;
    *s ^= *s >> 27;
    return *s * 2685821657736338717ULL;
}

static void gen_random(int *a, int n) {
    for (int i = 0; i < n; i++)
        a[i] = (int)(xorshift(&data_state) % (unsigned long long)(MAXKEY + 1));
}

static void swap_int(int *x, int *y) {
    int t = *x;
    *x = *y;
    *y = t;
}

static size_t round16(size_t x) {
    return (x + 15) / 16 * 16;
}

static size_t merge_frame_bytes(void) {
    return round16(2 * sizeof(int *) + 3 * sizeof(int) + 2 * sizeof(void *));
}

static size_t quick_frame_bytes(void) {
    return round16(sizeof(int *) + 4 * sizeof(int) + 2 * sizeof(void *));
}

static void merge_arrays(int *a, int *tmp, int lo, int mid, int hi) {
    int i = lo, j = mid + 1, k = lo;
    while (i <= mid && j <= hi) {
        g_comp++;
        if (a[i] <= a[j]) tmp[k++] = a[i++];
        else tmp[k++] = a[j++];
    }
    while (i <= mid) tmp[k++] = a[i++];
    while (j <= hi) tmp[k++] = a[j++];
    for (k = lo; k <= hi; k++) a[k] = tmp[k];
}

static void merge_sort_rec(int *a, int *tmp, int lo, int hi) {
    g_depth++;
    if (g_depth > g_maxdepth) g_maxdepth = g_depth;
    if (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        merge_sort_rec(a, tmp, lo, mid);
        merge_sort_rec(a, tmp, mid + 1, hi);
        merge_arrays(a, tmp, lo, mid, hi);
    }
    g_depth--;
}

static void merge_sort(int *a, int n) {
    int *tmp = (int *)malloc(sizeof(int) * (size_t)n);
    g_heap += (long long)sizeof(int) * n;
    merge_sort_rec(a, tmp, 0, n - 1);
    free(tmp);
}

static int median_of_three(const int *a, int lo, int hi) {
    int mid = lo + (hi - lo) / 2;
    int x = a[lo], y = a[mid], z = a[hi];
    g_comp++;
    if (x < y) {
        g_comp++;
        if (y < z) return mid;
        g_comp++;
        if (x < z) return hi;
        return lo;
    }
    g_comp++;
    if (x < z) return lo;
    g_comp++;
    if (y < z) return hi;
    return mid;
}

static int choose_pivot(const int *a, int lo, int hi, int type) {
    if (type == PIVOT_FIRST) return lo;
    if (type == PIVOT_RANDOM)
        return lo + (int)(xorshift(&pivot_state) % (unsigned long long)(hi - lo + 1));
    return median_of_three(a, lo, hi);
}

static int partition(int *a, int lo, int hi, int type) {
    int p = choose_pivot(a, lo, hi, type);
    swap_int(&a[lo], &a[p]);
    int pivot = a[lo];
    int i = lo;
    for (int j = lo + 1; j <= hi; j++) {
        g_comp++;
        if (a[j] < pivot) {
            i++;
            swap_int(&a[i], &a[j]);
        }
    }
    swap_int(&a[lo], &a[i]);
    return i;
}

static void quick_sort_rec(int *a, int lo, int hi, int type) {
    g_depth++;
    if (g_depth > g_maxdepth) g_maxdepth = g_depth;
    if (lo < hi) {
        int p = partition(a, lo, hi, type);
        quick_sort_rec(a, lo, p - 1, type);
        quick_sort_rec(a, p + 1, hi, type);
    }
    g_depth--;
}

static void run_algorithm(int alg, int *a, int n) {
    if (alg == ALG_MERGE) merge_sort(a, n);
    else quick_sort_rec(a, 0, n - 1, alg - 1);
}

static int is_sorted(const int *a, int n) {
    for (int i = 0; i + 1 < n; i++)
        if (a[i] > a[i + 1]) return 0;
    return 1;
}

static void checksum(const int *a, int n, long long *s1, long long *s2) {
    long long x = 0, y = 0;
    for (int i = 0; i < n; i++) {
        x += a[i];
        y += (long long)a[i] * a[i];
    }
    *s1 = x;
    *s2 = y;
}

static int verify(const int *result, const int *orig, int n) {
    long long a1, a2, b1, b2;
    checksum(result, n, &a1, &a2);
    checksum(orig, n, &b1, &b2);
    return is_sorted(result, n) && a1 == b1 && a2 == b2;
}

static long long memory_bytes(int alg) {
    size_t frame = (alg == ALG_MERGE) ? merge_frame_bytes() : quick_frame_bytes();
    return g_heap + (long long)g_maxdepth * (long long)frame;
}

static void measure_once(int alg, const int *orig, int *work, int n,
                         long long *comp, long long *mem, int *ok) {
    memcpy(work, orig, sizeof(int) * (size_t)n);
    pivot_state = PIVOT_SEED;
    g_comp = 0;
    g_heap = 0;
    g_depth = 0;
    g_maxdepth = 0;
    run_algorithm(alg, work, n);
    *comp = g_comp;
    *mem = memory_bytes(alg);
    *ok = verify(work, orig, n);
}

static double now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static double measure_time(int alg, const int *orig, int *work, int n) {
    double total = 0.0;
    for (int r = 0; r < WARMUP_RUNS + TIME_REPEAT; r++) {
        double t0, t1;
        memcpy(work, orig, sizeof(int) * (size_t)n);
        pivot_state = PIVOT_SEED;
        g_depth = 0;
        t0 = now_ms();
        run_algorithm(alg, work, n);
        t1 = now_ms();
        if (r >= WARMUP_RUNS) total += t1 - t0;
    }
    return total / TIME_REPEAT;
}

int main(void) {
    int maxn = SIZES[NSIZES - 1];
    int *orig = (int *)malloc(sizeof(int) * (size_t)maxn);
    int *work = (int *)malloc(sizeof(int) * (size_t)maxn);
    int all_ok = 1;

    printf("Frame model : merge = %d bytes, quick = %d bytes per recursive call\n",
           (int)merge_frame_bytes(), (int)quick_frame_bytes());
    printf("Time = average of %d runs (copy time excluded)\n\n", TIME_REPEAT);

    for (int s = 0; s < NSIZES; s++) {
        int n = SIZES[s];
        gen_random(orig, n);
        printf("Data Size : %d\n\n", n);
        printf("Algorithm       Comparisons    Time(ms)    Memory(Bytes)\n");
        printf("--------------------------------------------------------\n");
        for (int alg = 0; alg < NALG; alg++) {
            long long comp, mem;
            int ok;
            double ms;
            measure_once(alg, orig, work, n, &comp, &mem, &ok);
            if (!ok) all_ok = 0;
            ms = measure_time(alg, orig, work, n);
            printf("%-15s %-14lld %-11.4f %lld\n", ALG_LABEL[alg], comp, ms, mem);
        }
        printf("\n");
    }
    printf("All results sorted correctly : %s\n\n", all_ok ? "YES" : "NO");
    printf("------------------------------------\n\n");

    printf("[Supplementary] Average over %d random datasets\n\n", SUPP_TRIALS);
    for (int s = 0; s < NSIZES; s++) {
        int n = SIZES[s];
        long long total_comp[NALG] = {0, 0, 0, 0};
        long long total_mem[NALG] = {0, 0, 0, 0};
        for (int k = 0; k < SUPP_TRIALS; k++) {
            gen_random(orig, n);
            for (int alg = 0; alg < NALG; alg++) {
                long long comp, mem;
                int ok;
                measure_once(alg, orig, work, n, &comp, &mem, &ok);
                if (!ok) all_ok = 0;
                total_comp[alg] += comp;
                total_mem[alg] += mem;
            }
        }
        printf("Data Size : %d\n\n", n);
        printf("Algorithm       Avg.Comparisons   Avg.Memory(Bytes)\n");
        printf("--------------------------------------------------\n");
        for (int alg = 0; alg < NALG; alg++)
            printf("%-15s %-17.1f %.1f\n", ALG_LABEL[alg],
                   (double)total_comp[alg] / SUPP_TRIALS,
                   (double)total_mem[alg] / SUPP_TRIALS);
        printf("\n");
    }
    printf("All supplementary results sorted correctly : %s\n", all_ok ? "YES" : "NO");

    free(orig);
    free(work);
    return 0;
}
