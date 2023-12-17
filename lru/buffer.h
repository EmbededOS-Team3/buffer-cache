#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#define BLOCK_SIZE    4096

#include <time.h>
#include <math.h>


char *disk_buffer;
int disk_fd;
double start_time, end_time;
pthread_mutex_t buffer_lock;

// Linux cli dd to make diskfile
// dd if=/dev/zero of=diskfile count=1000 bs=1000

typedef struct buffer_entry buffer_entry_t;
struct buffer_entry {
    int block_nr;
    char data[BLOCK_SIZE];
    int is_dirty;
    double timestamp;
    buffer_entry_t *next;
};

buffer_entry_t *head = NULL;
buffer_entry_t *tail = NULL;

int num_blocks_in_buffer = 0;
int max_buffer_size = 10;

void start_timer() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    start_time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

double end_timer() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    end_time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    return end_time - start_time;
}

void flush_thread(void *arg) {
    // buffer lock
    pthread_mutex_lock(&buffer_lock);

    buffer_entry_t *current = head;

    // 모든 dirty block을 디스크에 씀
    while (current != NULL) {
        if (current->is_dirty) {
            int ret = lseek(disk_fd, current->block_nr * BLOCK_SIZE, SEEK_SET);
            if (ret < 0) {
                pthread_mutex_unlock(&buffer_lock);
                return;
            }

            ret = write(disk_fd, current->data, BLOCK_SIZE);
            if (ret < 0) {
                pthread_mutex_unlock(&buffer_lock);
                return;
            }

            current->is_dirty = 0;
        }
        current = current->next;
    }

    // buffer unlock
    pthread_mutex_unlock(&buffer_lock);
}

void add_to_buffer(int block_nr, char *data) {
    // 버퍼가 꽉 차있는지 확인
    if (num_blocks_in_buffer == max_buffer_size) {

        // 버퍼가 꽉 찬 경우

        // 1. victim 선정 (LRU - oldest timestamp)
        buffer_entry_t *victim = head;
        buffer_entry_t *current = head->next;

        while (current != NULL) {
            if (current->timestamp < victim->timestamp) {
                victim = current;
            }
            current = current->next;
        }

        // 2. victim block을 디스크에 씀
        int ret = lseek(disk_fd, victim->block_nr * BLOCK_SIZE, SEEK_SET);

        if (ret < 0) {
            return;
        }

        ret = write(disk_fd, victim->data, BLOCK_SIZE);
        if (ret < 0) {
            return;
        }

        // 3. victim block을 버퍼에서 제거
        if (victim == head) {
            head = head->next;
        } else {
            buffer_entry_t *prev = head;
            while (prev->next != victim) {
                prev = prev->next;
            }
            prev->next = victim->next;
        }

        free(victim);
        num_blocks_in_buffer--;
    }

    // 버퍼에 block 추가
    buffer_entry_t *entry = malloc(sizeof(buffer_entry_t));
    entry->block_nr = block_nr;
    memcpy(entry->data, data, BLOCK_SIZE);
    entry->next = NULL;
    entry->is_dirty = 1;
    entry->timestamp = time(NULL);

    if (head == NULL) {
        head = tail = entry;
    } else {
        tail->next = entry;
        tail = entry;
    }

    num_blocks_in_buffer++;

    pthread_mutex_unlock(&buffer_lock);

    if (entry->is_dirty) {
        pthread_t flush_thread_id;
        pthread_create(&flush_thread_id, NULL, (void *(*)(void *)) flush_thread, NULL);
    }
}




int is_block_in_buffer(int block_nr) {
    buffer_entry_t *entry = head;
    while (entry != NULL) {
        if (entry->block_nr == block_nr) {
            return 1;
        }
        entry = entry->next;
    }
    return 0;
}


char *get_block_from_buffer(int block_nr) {
    if (!is_block_in_buffer(block_nr)) {
        return NULL;
    }

    buffer_entry_t *entry = head;

    while (entry != NULL) {
        if (entry->block_nr == block_nr) {
            return entry->data;
        }
        entry = entry->next;
    }

    return NULL;
}

void remove_from_buffer(int block_nr) {
    // block이 버퍼에 있는지 확인
    if (!is_block_in_buffer(block_nr)) {
        return;
    }

    // block을 버퍼에서 찾음
    char *block_data = get_block_from_buffer(block_nr);

    // 버퍼에서 찾은 block_data에 해당하는 buffer_entry_t 찾음
    buffer_entry_t *entry = head;
    while (entry != NULL) {
        if (entry->data == block_data) {
            break;
        }
        entry = entry->next;
    }

    // block을 버퍼에서 제거
    if (entry == head) {
        head = head->next;
    } else {
        buffer_entry_t *prev = head;
        while (prev->next != entry) {
            prev = prev->next;
        }
        prev->next = entry->next;
    }

    // block 메모리 해제
    free(entry);
    num_blocks_in_buffer--;
}

void set_block_dirty(int block_nr) {
    if (!is_block_in_buffer(block_nr)) {
        return;
    }

    buffer_entry_t *entry = head;

    while (entry != NULL) {
        if (entry->block_nr == block_nr) {
            entry->is_dirty = 1;
            return;
        }
        entry = entry->next;
    }
}


int os_read(int block_nr, char *user_buffer) {
    // 버퍼에 block이 있는 지 확인
    if (is_block_in_buffer(block_nr)) {
        // 있는 경우 Buffer에서 읽어옴
        memcpy(user_buffer, get_block_from_buffer(block_nr), BLOCK_SIZE);
        return BLOCK_SIZE;
    }


    // 디스크에서 데이터를 읽음
    int ret = lseek(disk_fd, block_nr * BLOCK_SIZE, SEEK_SET);
    if (ret < 0) {
        return ret;
    }

    ret = read(disk_fd, disk_buffer, BLOCK_SIZE);
    if (ret < 0) {
        return ret;
    }

    // 블럭을 버퍼에 추가
    add_to_buffer(block_nr, disk_buffer);

    // user_buffer에 데이터를 복사
    memcpy(user_buffer, disk_buffer, BLOCK_SIZE);

    return 1024;
}

int os_write(int block_nr, char *user_buffer) {
    // user_buffer의 데이터를 버퍼에 추가
    memcpy(get_block_from_buffer(block_nr), user_buffer, BLOCK_SIZE);

    // 버퍼에 dirty block 표시
    set_block_dirty(block_nr);

    if (num_blocks_in_buffer == max_buffer_size) {
        // Victim selection
        buffer_entry_t *victim = head;
        remove_from_buffer(victim->block_nr);

        // Write victim block to disk
        int ret = lseek(disk_fd, victim->block_nr * BLOCK_SIZE, SEEK_SET);
        if (ret < 0) {
            return ret;
        }

        ret = write(disk_fd, victim->data, BLOCK_SIZE);
        if (ret < 0) {
            return ret;
        }
    }

    return BLOCK_SIZE;
}

int lib_read(int block_nr, char *user_buffer) {
    int ret;
    start_timer();
    ret = os_read(block_nr, user_buffer);
    double read_time = end_timer();

    if (ret == BLOCK_SIZE) {
        // hit
        printf("Hit block: %d, read time: %.0f us\n", block_nr, read_time);
    } else {
        // miss
        printf("Miss block: %d, read time: %.0f us\n", block_nr, read_time);
    }

    return ret;
}

int lib_write(int block_nr, char *user_buffer) {
    int ret;
    ret = os_write(block_nr, user_buffer);


    return ret;
}

int init() {
    disk_buffer = aligned_alloc(BLOCK_SIZE, BLOCK_SIZE);
    if (disk_buffer == NULL)
        return -errno;

    printf("disk_buffer: %p\n", disk_buffer);

    disk_fd = open("diskfile", O_RDWR | __O_DIRECT);
    printf("disk_fd: %d\n", disk_fd);
    if (disk_fd < 0)
        return disk_fd;
}

//int main(int argc, char *argv[]) {
//    char *buffer;
//    int ret;
//
//    init();
//
//    buffer = malloc(BLOCK_SIZE);
//
//    ret = lib_read(0, buffer);
//    printf("nread: %d\n", ret);
//
//    ret = lib_write(0, buffer);
//    printf("nwrite: %d\n", ret);
//
//    return 0;
//}

