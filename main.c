#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define SHM_SIZE 1024

void handle_end(int signum) {
    key_t key = ftok("/myshm", 'R'); // получаем ключ разделяемой памяти
    int shmid = shmget(key, 40, 0666 | IPC_CREAT);
    if (shmdt(shmat(shmid, NULL, 0)) == -1) {
        perror("shmdt failed");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < 40; ++i) {
        char semaphore_name[20];
        sprintf(semaphore_name, "/semaphore_%d", i);
        sem_unlink(semaphore_name);
    }

    sem_unlink("write_shr_memory");
    printf("\nexited and done cleanup\n");
    exit(0);
}

int main(int argc, char* argv[]) {
    int fd;
    char *ptr;
    pid_t pid;

    signal(SIGTERM, handle_end);

    sem_t *semaphores[40];

    for (int i = 0; i < 40; ++i) {
        char semaphore_name[20];
        sprintf(semaphore_name, "/semaphore_%d", i);

        semaphores[i] = sem_open(semaphore_name, O_CREAT, 0666, 1);
    }

    sem_t* write_shr_memory = sem_open("write_shr_memory", O_CREAT, 0666, 0);

    // Создание объекта разделяемой памяти
    fd = shm_open("/myshm", O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        exit(1);
    }

    // Установка размера объекта разделяемой памяти
    if (ftruncate(fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        exit(1);
    }

    // Отображение объекта разделяемой памяти в адресное пространство процесса
    ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Создание дочерних процессов цветков
    for (int i = 0; i < 40; ++i) {
        pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            // Дочерний процесс записывает данные в разделяемую память
            srand(time(NULL));
            ptr[i] = ((i * rand()) % 255) + 1;
            sem_post(write_shr_memory);
            exit(0);
        }
    }
    // Родительский процесс ждет, пока дочерний процесс завершит запись
    for (int i = 0; i < 40; ++i) {
        sem_wait(write_shr_memory);
    }

    // Создание дочерних процессов садовников
    for (int i = 0; i < 2; ++i) {
        pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            srand(time(NULL));
            int time = 0;
            while (1) {
                usleep(1000000 / atoi(argv[1]));
                time = (time + 1) % 256;
                for (int j = 0; j < 40; ++j) {
                    sem_wait(semaphores[j]);
                    unsigned char arr[40];
                    memcpy(arr, ptr, sizeof (arr));
                    if (arr[j] == time) {
                        unsigned char new_value = ((rand() * 2) % 255) + 1;
                        if (time == new_value) {
                            new_value = ((rand() * 2) % 255) + 1;
                        }
                        ptr[j] = new_value;
                        printf("Gardener number %d watered flower number %d at timing %d, new timing is %u \n", i + 1, j + 1, time, new_value);
                    }
                    sem_post(semaphores[j]);
                }
            }
        }
    }

    while (1) {
        usleep(1000000 / atoi(argv[1]));
        unsigned char arr[40];
        memcpy(arr, ptr, sizeof (arr));
        printf("\n");
        for (int i = 0; i < 40; ++i) {
            if (i % 10 == 0) {
                printf("\n");
            }
            printf("%u ", arr[i]);
        }
    }
}
