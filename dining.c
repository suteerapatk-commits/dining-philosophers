/*
 * Dining Philosophers Simulator  (OS Mini Project - Group Ulike)
 *
 * Compile : gcc dining.c -o dining -pthread
 * Run     : ./dining
 *
 * Mode 1 : Naive        - หยิบซ้ายแล้วหยิบขวา (เกิด Deadlock ได้)
 * Mode 2 : Resource Ordering - หยิบ Fork เบอร์ต่ำก่อนเสมอ (ป้องกัน Deadlock)
 * Mode 3 : Waiter (Semaphore) - ให้กินพร้อมกันได้สูงสุด N-1 คน (ป้องกัน Deadlock)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>

#define MAX_PHILOSOPHERS 10
#define DEADLOCK_TIMEOUT 5      /* ถ้าไม่มีใครกินเลย 5 วินาที = Deadlock */

/* ---------- ตัวแปรส่วนกลาง ---------- */
pthread_mutex_t forks[MAX_PHILOSOPHERS];      /* Fork 1 อัน = mutex 1 ตัว */
pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER; /* กันข้อความปนกัน */
sem_t waiter;                                 /* ใช้เฉพาะ Mode 3 */

int num_philosophers;
int rounds;
int mode;

/* ---------- สถิติ ---------- */
int    eat_count[MAX_PHILOSOPHERS];   /* กินไปกี่ครั้ง */
double total_wait[MAX_PHILOSOPHERS];  /* เวลารวมที่รอ Fork (วินาที) */
double max_wait[MAX_PHILOSOPHERS];    /* เวลารอนานสุดครั้งเดียว */

volatile int    finished_count = 0;   /* จำนวน Philosopher ที่ทำครบแล้ว */
volatile time_t last_activity;        /* เวลาล่าสุดที่มีคนกินเสร็จ */

/* ---------- ฟังก์ชันช่วย ---------- */
double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void log_state(int id, const char *msg)
{
    pthread_mutex_lock(&print_lock);
    printf("Philosopher %d : %s\n", id + 1, msg);
    pthread_mutex_unlock(&print_lock);
}

void random_sleep(unsigned int *seed)      /* หลับ 0.1 - 0.5 วินาที */
{
    usleep(100000 + rand_r(seed) % 400000);
}

/* ---------- Thread ของ Philosopher ---------- */
void *philosopher(void *arg)
{
    int id    = *(int *)arg;
    int left  = id;
    int right = (id + 1) % num_philosophers;
    unsigned int seed = (unsigned int)time(NULL) + id;

    for (int i = 0; i < rounds; i++)
    {
        /* 1) Thinking */
        log_state(id, "Thinking");
        random_sleep(&seed);

        /* 2) Hungry -> เริ่มจับเวลารอ */
        log_state(id, "Hungry");
        double start = now_sec();

        if (mode == 1)
        {
            /* Naive: หยิบซ้ายก่อน แล้วหยิบขวา */
            pthread_mutex_lock(&forks[left]);
            log_state(id, "picked LEFT fork");
            usleep(200000);   /* หน่วงเวลาให้ Deadlock เกิดง่ายขึ้น (เพื่อสาธิต) */
            pthread_mutex_lock(&forks[right]);
        }
        else if (mode == 2)
        {
            /* Resource Ordering: หยิบ Fork เบอร์น้อยก่อนเสมอ */
            int first  = (left < right) ? left : right;
            int second = (left < right) ? right : left;
            pthread_mutex_lock(&forks[first]);
            pthread_mutex_lock(&forks[second]);
        }
        else
        {
            /* Waiter: ขออนุญาตก่อน (อนุญาตแค่ N-1 คน) */
            sem_wait(&waiter);
            pthread_mutex_lock(&forks[left]);
            pthread_mutex_lock(&forks[right]);
        }

        /* บันทึกเวลาที่รอ */
        double waited = now_sec() - start;
        total_wait[id] += waited;
        if (waited > max_wait[id]) max_wait[id] = waited;

        /* 3) Eating */
        log_state(id, "Eating");
        eat_count[id]++;
        last_activity = time(NULL);
        random_sleep(&seed);

        /* คืน Fork */
        pthread_mutex_unlock(&forks[right]);
        pthread_mutex_unlock(&forks[left]);
        if (mode == 3) sem_post(&waiter);
    }

    finished_count++;
    return NULL;
}

/* ---------- แสดงสถิติ ---------- */
void print_statistics(double elapsed, int deadlock)
{
    printf("\n===== Result =====\n");
    printf("%-14s %-8s %-14s %-14s\n", "Philosopher", "Ate", "Avg wait(s)", "Max wait(s)");

    int total = 0;
    for (int i = 0; i < num_philosophers; i++)
    {
        double avg = eat_count[i] ? total_wait[i] / eat_count[i] : 0;
        printf("%-14d %-8d %-14.2f %-14.2f\n", i + 1, eat_count[i], avg, max_wait[i]);
        total += eat_count[i];
    }

    printf("\nTotal meals    : %d (expected %d)\n", total, num_philosophers * rounds);
    printf("Elapsed time   : %.2f seconds\n", elapsed);

    if (deadlock)
        printf("Status         : DEADLOCK! (ทุกคนถือ Fork ค้างและรอกันเอง)\n");
    else
        printf("Status         : OK - no deadlock, no starvation\n");
}

/* ---------- main ---------- */
int main()
{
    pthread_t threads[MAX_PHILOSOPHERS];
    int ids[MAX_PHILOSOPHERS];

    printf("===== Dining Philosophers =====\n\n");
    printf("Enter number of philosophers (2-%d): ", MAX_PHILOSOPHERS);
    scanf("%d", &num_philosophers);
    printf("Enter number of rounds: ");
    scanf("%d", &rounds);
    printf("\nSelect mode:\n");
    printf("  1 = Naive (Deadlock can happen)\n");
    printf("  2 = Resource Ordering (Deadlock prevention)\n");
    printf("  3 = Waiter / Semaphore (Deadlock prevention)\n");
    printf("Mode: ");
    scanf("%d", &mode);

    if (num_philosophers < 2 || num_philosophers > MAX_PHILOSOPHERS ||
        rounds < 1 || mode < 1 || mode > 3)
    {
        printf("Invalid input.\n");
        return 1;
    }

    /* เตรียมค่าเริ่มต้น */
    for (int i = 0; i < num_philosophers; i++)
    {
        pthread_mutex_init(&forks[i], NULL);
        eat_count[i]  = 0;
        total_wait[i] = 0;
        max_wait[i]   = 0;
    }
    sem_init(&waiter, 0, num_philosophers - 1);   /* N-1 คน */
    last_activity = time(NULL);

    printf("\nStarting simulation...\n\n");
    double start = now_sec();

    for (int i = 0; i < num_philosophers; i++)
    {
        ids[i] = i;
        pthread_create(&threads[i], NULL, philosopher, &ids[i]);
    }

    /* Watchdog: ตรวจ Deadlock ถ้าไม่มีใครกินเลยเกิน DEADLOCK_TIMEOUT วินาที */
    while (finished_count < num_philosophers)
    {
        sleep(1);
        if (finished_count < num_philosophers &&
            time(NULL) - last_activity >= DEADLOCK_TIMEOUT)
        {
            printf("\n*** DEADLOCK DETECTED: no progress for %d seconds ***\n",
                   DEADLOCK_TIMEOUT);
            print_statistics(now_sec() - start, 1);
            printf("\nSimulation finished.\n");
            return 0;   /* thread ที่ค้างจะถูกปิดพร้อมโปรแกรม */
        }
    }

    for (int i = 0; i < num_philosophers; i++)
        pthread_join(threads[i], NULL);

    print_statistics(now_sec() - start, 0);

    for (int i = 0; i < num_philosophers; i++)
        pthread_mutex_destroy(&forks[i]);
    sem_destroy(&waiter);

    printf("\nSimulation finished.\n");
    return 0;
}
