#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include "buffer.h"

#define BLOCK_SIZE    4096
#define MAX_BUFFER_SIZE    10


void generate_normal_distribution(int *sequence, int sequence_length) {
    double mean = 50;
    double stddev = 10;

    unsigned int seed = (unsigned int)time(NULL);

    for (int i = 0; i < sequence_length; i++) {
        double rand_value = (double)rand_r(&seed) / (RAND_MAX + 1.0);

        sequence[i] = (int)round(mean + stddev * rand_value);
    }
}

void generate_zipfian_distribution(int *sequence, int sequence_length, double alpha) {
    // 정규화 상수 계산
    long double sum = 0.0;
    for (int i = 1; i <= sequence_length; i++) {
        sum += pow(i, -alpha);
    }
    long double normalization_constant = 1.0 / sum;

    // Zipfian 분포 생성
    for (int i = 0; i < sequence_length; i++) {
        double random_value = ((double)rand() / (RAND_MAX + 1.0)) * normalization_constant;
        double value = 0.0;
        int rank = 0;
        while (random_value > 0.0) {
            double prob = pow(rank + 1, -alpha) - pow(rank, -alpha);
            if (random_value >= prob) {
                random_value -= prob;
            } else {
                break;
            }
            rank++;
        }
        if (rank < sequence_length) {
            sequence[i] = rank;
        } else {
            break;
        }
    }
}





int main() {
    int sequence_length = 10000;
    int *sequence;

    char *buffer;

    double alpha = 0.7;

    // Normal distribution을 따르는 Block Access Sequence 생성
    sequence = malloc(sizeof(int) * sequence_length);
    generate_normal_distribution(sequence, sequence_length);

    // Zipfian distribution을 따르는 Block Access Sequence 생성
    // sequence = malloc(sizeof(int) * sequence_length);
    // generate_zipfian_distribution(sequence, sequence_length, alpha);


    // Buffer 초기화
    init();

    buffer = malloc(BLOCK_SIZE);

    // Block Access Sequence를 따라 Read 수행
    int hit_count = 0;
    int miss_count = 0;
    struct timeval start, end;
    for (int i = 0; i < sequence_length; i++) {
        int block_nr = sequence[i];
        int ret = lib_read(block_nr, buffer);
        if (ret == BLOCK_SIZE) {
            hit_count++;
        }
        else {
            miss_count++;
        }
    }

    // Hit 비율 계산
    double hit_rate = (double)hit_count / sequence_length;

    // 결과 출력
    printf("Hit rate: %.2f%%\n", hit_rate * 100);
    printf("hit_count: %d\n", hit_count);
    printf("miss_count: %d\n", miss_count);

    return 0;
}

