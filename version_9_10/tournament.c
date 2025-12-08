#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdatomic.h>

#define MAX_FIGHTERS 32

typedef enum {
    ROCK = 0,
    SCISSORS = 1,
    PAPER = 2
} HandSign;

typedef struct {
    int id;
    atomic_int active;
    atomic_int victories;
    atomic_int gesture;
    atomic_int has_rival;
    atomic_int rival_id;
    pthread_t thread_id;
} Combatant;

typedef struct {
    Combatant fighters[MAX_FIGHTERS];
    int total_count;
    atomic_int alive_count;
    atomic_int round_num;
    atomic_int finished;
    pthread_spinlock_t arena_spinlock;
    pthread_barrier_t round_barrier;
} Arena;

Arena arena;
int fighter_count;
pthread_t fighter_threads[MAX_FIGHTERS];
FILE* output_file = NULL;
int use_file_output = 0;

void print_output(const char* format, ...) {
    va_list args1, args2;
    va_start(args1, format);
    
    vprintf(format, args1);
    
    if (use_file_output && output_file) {
        va_start(args2, format);
        vfprintf(output_file, format, args2);
        va_end(args2);
    }
    
    va_end(args1);
}

HandSign get_winner(HandSign sign1, HandSign sign2) {
    if (sign1 == sign2) return (HandSign)-1;

    if ((sign1 == ROCK && sign2 == SCISSORS) ||
        (sign1 == SCISSORS && sign2 == PAPER) ||
        (sign1 == PAPER && sign2 == ROCK)) {
        return sign1;
    }
    return sign2;
}

const char* gesture_name(HandSign sign) {
    switch(sign) {
        case ROCK: return "Камень";
        case SCISSORS: return "Ножницы";
        case PAPER: return "Бумага";
        default: return "Неизвестно";
    }
}

void setup_round() {
    pthread_spin_lock(&arena.arena_spinlock);

    if (atomic_load(&arena.finished)) {
        pthread_spin_unlock(&arena.arena_spinlock);
        return;
    }

    int ready_fighters[MAX_FIGHTERS];
    int count = 0;

    for (int i = 0; i < arena.total_count; i++) {
        if (atomic_load(&arena.fighters[i].active) && 
            !atomic_load(&arena.fighters[i].has_rival)) {
            ready_fighters[count++] = i;
        }
    }

    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = ready_fighters[i];
        ready_fighters[i] = ready_fighters[j];
        ready_fighters[j] = temp;
    }

    for (int i = 0; i < count - 1; i += 2) {
        int fighter1 = ready_fighters[i];
        int fighter2 = ready_fighters[i + 1];

        atomic_store(&arena.fighters[fighter1].has_rival, 1);
        atomic_store(&arena.fighters[fighter1].rival_id, fighter2);
        atomic_store(&arena.fighters[fighter2].has_rival, 1);
        atomic_store(&arena.fighters[fighter2].rival_id, fighter1);

        print_output("Организован бой: Боец %d vs Боец %d\n", fighter1, fighter2);
    }

    atomic_fetch_add(&arena.round_num, 1);
    print_output("Начало раунда %d. Бойцов готово к бою: %d\n", 
                 atomic_load(&arena.round_num), count);

    pthread_spin_unlock(&arena.arena_spinlock);
}

void* fighter_thread(void* arg) {
    int fighter_id = *(int*)arg;
    free(arg);
    
    srand(time(NULL) + fighter_id + pthread_self());
    
    print_output("Боец %d (Поток %lu) начал участие в турнире.\n", 
                 fighter_id, (unsigned long)pthread_self());

    while (1) {
        // Ожидание начала раунда с использованием барьера
        pthread_barrier_wait(&arena.round_barrier);
        
        if (atomic_load(&arena.finished)) {
            break;
        }

        if (!atomic_load(&arena.fighters[fighter_id].active)) {
            print_output("Боец %d выбыл из турнира.\n", fighter_id);
            break;
        }

        if (atomic_load(&arena.fighters[fighter_id].has_rival)) {
            int rival_id = atomic_load(&arena.fighters[fighter_id].rival_id);

            if (rival_id < 0 || rival_id >= arena.total_count ||
                !atomic_load(&arena.fighters[rival_id].active)) {
                atomic_store(&arena.fighters[fighter_id].has_rival, 0);
                atomic_store(&arena.fighters[fighter_id].rival_id, -1);
                continue;
            }

            // Только боец с меньшим ID начинает бой
            if (fighter_id > rival_id) {
                atomic_store(&arena.fighters[fighter_id].has_rival, 0);
                atomic_store(&arena.fighters[fighter_id].rival_id, -1);
                continue;
            }

            HandSign my_move;
            HandSign rival_move;
            HandSign winner_move;
            int duel_rounds = 0;

            do {
                duel_rounds++;
                my_move = rand() % 3;
                rival_move = rand() % 3;
                winner_move = get_winner(my_move, rival_move);

                // Атомарное обновление жестов
                atomic_store(&arena.fighters[fighter_id].gesture, my_move);
                atomic_store(&arena.fighters[rival_id].gesture, rival_move);

                print_output("Бой %d vs %d (раунд %d): %s vs %s => ",
                    fighter_id, rival_id, duel_rounds,
                    gesture_name(my_move), gesture_name(rival_move));

                if (winner_move == my_move) {
                    print_output("Победил Боец %d\n", fighter_id);
                    atomic_fetch_add(&arena.fighters[fighter_id].victories, 1);
                    atomic_store(&arena.fighters[rival_id].active, 0);
                    atomic_fetch_sub(&arena.alive_count, 1);
                } else if (winner_move == rival_move) {
                    print_output("Победил Боец %d\n", rival_id);
                    atomic_fetch_add(&arena.fighters[rival_id].victories, 1);
                    atomic_store(&arena.fighters[fighter_id].active, 0);
                    atomic_fetch_sub(&arena.alive_count, 1);
                } else {
                    print_output("Ничья\n");
                    usleep(300000);
                }
            } while (winner_move == (HandSign)-1 && 
                     atomic_load(&arena.fighters[fighter_id].active) && 
                     atomic_load(&arena.fighters[rival_id].active));

            atomic_store(&arena.fighters[fighter_id].has_rival, 0);
            atomic_store(&arena.fighters[fighter_id].rival_id, -1);
            atomic_store(&arena.fighters[rival_id].has_rival, 0);
            atomic_store(&arena.fighters[rival_id].rival_id, -1);
        }
        
        usleep(10000);
    }

    print_output("Боец %d завершил участие.\n", fighter_id);
    return NULL;
}

void print_active_fighters() {
    print_output("\nПромежуточные победители: ");
    int first = 1;
    for (int i = 0; i < arena.total_count; i++) {
        if (atomic_load(&arena.fighters[i].active)) {
            if (!first) {
                print_output(", ");
            }
            print_output("Боец %d", i);
            first = 0;
        }
    }
    print_output("\n");
}

void cleanup() {
    print_output("Очистка ресурсов.\n");
    
    atomic_store(&arena.finished, 1);
    
    for (int i = 0; i < fighter_count; i++) {
        if (fighter_threads[i]) {
            pthread_join(fighter_threads[i], NULL);
        }
    }
    
    pthread_spin_destroy(&arena.arena_spinlock);
    pthread_barrier_destroy(&arena.round_barrier);
    
    if (output_file) {
        fclose(output_file);
        output_file = NULL;
    }
}

void signal_handler(int sig) {
    print_output("\nТурнир остановлен по сигналу %d.\n", sig);
    cleanup();
    exit(0);
}

void print_usage(const char* prog_name) {
    printf("Использование (атомарная версия):\n");
    printf("  %s <количество_бойцов>                 - ввод из командной строки\n", prog_name);
    printf("  %s -f <файл_конфигурации>             - ввод из файла\n", prog_name);
    printf("  %s -f <файл_конфигурации> -o <файл>   - ввод из файла и вывод в файл\n", prog_name);
    printf("\nПримеры:\n");
    printf("  %s 8\n", prog_name);
    printf("  %s -f config.txt\n", prog_name);
    printf("  %s -f config.txt -o results_atomic.txt\n", prog_name);
}

int main(int argc, char *argv[]) {
    char* config_file = NULL;
    char* output_filename = NULL;
    int read_from_file = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            config_file = argv[i + 1];
            read_from_file = 1;
            i++;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_filename = argv[i + 1];
            use_file_output = 1;
            i++;
        } else if (argv[i][0] != '-') {
            fighter_count = atoi(argv[i]);
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (read_from_file && config_file) {
        FILE* config = fopen(config_file, "r");
        if (!config) {
            perror("Ошибка открытия файла конфигурации");
            return 1;
        }
        if (fscanf(config, "%d", &fighter_count) != 1) {
            printf("Ошибка чтения количества бойцов из файла\n");
            fclose(config);
            return 1;
        }
        fclose(config);
        printf("Прочитано из файла %s: %d бойцов\n", config_file, fighter_count);
    } else if (fighter_count == 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (fighter_count < 2 || fighter_count > MAX_FIGHTERS) {
        printf("Количество бойцов должно быть от 2 до %d\n", MAX_FIGHTERS);
        return 1;
    }
    
    if (use_file_output && output_filename) {
        output_file = fopen(output_filename, "w");
        if (!output_file) {
            perror("Ошибка открытия файла для вывода");
            return 1;
        }
        printf("Вывод будет сохранен в файл: %s\n", output_filename);
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    srand(time(NULL));

    print_output("------ Турнир 'Камень-Ножницы-Бумага' (атомарная версия) ------\n");
    print_output("Количество участников: %d\n", fighter_count);
    print_output("Используемые примитивы: атомарные переменные, спин-блокировки, барьеры\n");

    memset(&arena, 0, sizeof(Arena));
    arena.total_count = fighter_count;
    atomic_store(&arena.alive_count, fighter_count);
    atomic_store(&arena.round_num, 0);
    atomic_store(&arena.finished, 0);
    
    pthread_spin_init(&arena.arena_spinlock, PTHREAD_PROCESS_PRIVATE);
    pthread_barrier_init(&arena.round_barrier, NULL, fighter_count + 1);

    for (int i = 0; i < fighter_count; i++) {
        arena.fighters[i].id = i;
        atomic_store(&arena.fighters[i].active, 1);
        atomic_store(&arena.fighters[i].victories, 0);
        atomic_store(&arena.fighters[i].gesture, ROCK);
        atomic_store(&arena.fighters[i].has_rival, 0);
        atomic_store(&arena.fighters[i].rival_id, -1);
    }

    print_output("Создание потоков-бойцов...\n");
    for (int i = 0; i < fighter_count; i++) {
        int* fighter_id = malloc(sizeof(int));
        *fighter_id = i;
        
        if (pthread_create(&fighter_threads[i], NULL, fighter_thread, fighter_id) != 0) {
            perror("Ошибка создания потока");
            cleanup();
            return 1;
        }
    }

    sleep(2);
    
    print_output("\n------ Турнир начинается! ------\n");

    int round = 0;
    while (!atomic_load(&arena.finished)) {
        int active = atomic_load(&arena.alive_count);

        if (active <= 1) {
            atomic_store(&arena.finished, 1);
            break;
        }

        print_output("\n--- Раунд %d ---\n", ++round);
        print_output("Активных бойцов: %d\n", active);

        setup_round();
        
        // Ожидание завершения текущего раунда
        pthread_barrier_wait(&arena.round_barrier);
        sleep(3);

        // Проверка активных боев
        int duels_active;
        int max_waits = 30;
        do {
            duels_active = 0;
            for (int i = 0; i < fighter_count; i++) {
                if (atomic_load(&arena.fighters[i].has_rival)) {
                    duels_active = 1;
                    break;
                }
            }
            if (duels_active) {
                sleep(1);
                max_waits--;
            }
        } while (duels_active && max_waits > 0);

        print_active_fighters();
        
        // Сигнализируем о начале нового раунда
        pthread_barrier_wait(&arena.round_barrier);
    }

    atomic_store(&arena.finished, 1);
    
    int winner_found = 0;
    for (int i = 0; i < fighter_count; i++) {
        if (atomic_load(&arena.fighters[i].active)) {
            print_output("\nТурнир завершен! Победитель: Боец %d\n", i);
            winner_found = 1;
            break;
        }
    }

    if (!winner_found) {
        print_output("\nТурнир завершен! Победитель не определен.\n");
    }

    print_output("Все бои завершены.\n");
    
    cleanup();
    
    return 0;
}