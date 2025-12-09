#define _POSIX_C_SOURCE 199309L  // для clock_gettime() и CLOCK_REALTIME
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>

#define MAX_FIGHTERS 32  // max количество бойцов

// возможные жесты в игре
typedef enum {
    ROCK = 0,  // камень
    SCISSORS = 1, // ножницы
    PAPER = 2  // бумага
} HandSign;

// структура бойца
typedef struct {
    int id; // ID бойца
    int active; // активен ли боец (1) или выбыл (0)
    int victories; // количество побед
    HandSign gesture; // текущий жест
    int has_rival; //есть ли соперник для боя
    int rival_id; // ID соперника
    pthread_t thread_id; // ID потока
} Combatant;

// арена турнира
typedef struct {
    Combatant fighters[MAX_FIGHTERS];  // массив всех бойцов
    int total_count;  // общее количество бойцов
    int alive_count;  // количество активных бойцов
    int round_num; // № текущего раунда
    int finished;  // флаг завершения турнира
    sem_t arena_sem; // семафор для защиты критических секций
    pthread_mutex_t round_mutex; // мьютекс для условной переменной
    pthread_cond_t round_cond; // условная переменная для синхронизации раундов
} Arena;

Arena arena; // глобальная арена
int fighter_count; // количество бойцов
pthread_t fighter_threads[MAX_FIGHTERS]; // ID потоков-бойцов
FILE* output_file = NULL; // файл для вывода результатов
int use_file_output = 0;  // флаг вывода в файл

// функция вывода в консоль и/или файл
void print_output(const char* format, ...) {
    va_list args1, args2;
    va_start(args1, format);
    vprintf(format, args1);  // вывод в консоль
    if (use_file_output && output_file) {
        va_start(args2, format);
        vfprintf(output_file, format, args2);  // вывод в файл
        va_end(args2);
    }
    va_end(args1);
}

// ожидание семафора с обработкой прерываний
void semaphore_wait(sem_t* sem) {
    int result;
    do {
        result = sem_wait(sem);
    } while (result == EINTR);  // повторяем если было прерывание
}

// освобождение семафора
void semaphore_post(sem_t* sem) {
    sem_post(sem);
}

// определение победителя в раунде
HandSign get_winner(HandSign sign1, HandSign sign2) {
    if (sign1 == sign2) {
        return (HandSign)-1;  // ничья
    }
    if ((sign1 == ROCK && sign2 == SCISSORS) ||
        (sign1 == SCISSORS && sign2 == PAPER) ||
        (sign1 == PAPER && sign2 == ROCK)) {
        return sign1;  // 1й игрок победил
    }
    return sign2; // 2й игрок победил
}

// получение имени жеста
const char* gesture_name(HandSign sign) {
    switch(sign) {
        case ROCK: return "Камень";
        case SCISSORS: return "Ножницы";
        case PAPER: return "Бумага";
        default: return "Неизвестно";
    }
}

// организация раунда турнира
void setup_round() {
    semaphore_wait(&arena.arena_sem); // захват семафора
    if (arena.finished) {
        semaphore_post(&arena.arena_sem);
        return;
    }
    
    // сбор активных бойцов без соперника
    int ready_fighters[MAX_FIGHTERS];
    int count = 0;
    for (int i = 0; i < arena.total_count; i++) {
        if (arena.fighters[i].active && !arena.fighters[i].has_rival) {
            ready_fighters[count++] = i;
        }
    }
    
    // перемешивание бойцов для случайного формироания пар
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = ready_fighters[i];
        ready_fighters[i] = ready_fighters[j];
        ready_fighters[j] = temp;
    }
    
    // формирование пар бойцов
    for (int i = 0; i < count - 1; i += 2) {
        int fighter1 = ready_fighters[i];
        int fighter2 = ready_fighters[i + 1];
        arena.fighters[fighter1].has_rival = 1;
        arena.fighters[fighter1].rival_id = fighter2;
        arena.fighters[fighter2].has_rival = 1;
        arena.fighters[fighter2].rival_id = fighter1;
        print_output("Организован бой: Боец %d vs Боец %d\n", fighter1, fighter2);
    }
    
    arena.round_num++;
    print_output("Начало раунда %d. Бойцов готово к бою: %d\n", arena.round_num, count);
    semaphore_post(&arena.arena_sem);  // освобождение семафора
    
    // оповещение всех потоков о начале раунда
    pthread_mutex_lock(&arena.round_mutex);
    pthread_cond_broadcast(&arena.round_cond);
    pthread_mutex_unlock(&arena.round_mutex);
}

// функция потока-бойца
void* fighter_thread(void* arg) {
    int fighter_id = *(int*)arg;
    free(arg);
    
    // уникальный seed для генератора случайных чисел каждого потока
    unsigned int seed = time(NULL) + fighter_id + pthread_self();
    print_output("Боец %d (Поток %lu) начал участие в турнире.\n",
           fighter_id, (unsigned long)pthread_self());
    
    while (1) {
        semaphore_wait(&arena.arena_sem);
        if (arena.finished) {  // проверка завершения турнира
            semaphore_post(&arena.arena_sem);
            break;
        }
        if (!arena.fighters[fighter_id].active) {  // боец выбыл
            semaphore_post(&arena.arena_sem);
            break;
        }
        
        if (arena.fighters[fighter_id].has_rival) {  // у бойца есть соперник
            int rival_id = arena.fighters[fighter_id].rival_id;
            
            // проверка валидности соперника
            if (rival_id < 0 || rival_id >= arena.total_count ||
                !arena.fighters[rival_id].active) {
                arena.fighters[fighter_id].has_rival = 0;
                arena.fighters[fighter_id].rival_id = -1;
                semaphore_post(&arena.arena_sem);
                continue;
            }
            
            // гарантируем что бой обрабатывает только боец с меньшим ID
            if (fighter_id > rival_id) {
                arena.fighters[fighter_id].has_rival = 0;
                arena.fighters[fighter_id].rival_id = -1;
                semaphore_post(&arena.arena_sem);
                continue;
            }
            
            HandSign my_move;
            HandSign rival_move;
            HandSign winner_move;
            int duel_rounds = 0;
            
            // цикл боя (+ продолжается при ничьей)
            do {
                // проверка активности бойцов перед каждым раундом
                if (!arena.fighters[fighter_id].active || 
                    !arena.fighters[rival_id].active) {
                    arena.fighters[fighter_id].has_rival = 0;
                    arena.fighters[fighter_id].rival_id = -1;
                    arena.fighters[rival_id].has_rival = 0;
                    arena.fighters[rival_id].rival_id = -1;
                    semaphore_post(&arena.arena_sem);
                    break;
                }
                
                duel_rounds++;
                my_move = rand_r(&seed) % 3; // генерация жеста текущего бойца
                rival_move = rand_r(&seed) % 3; // генерация жеста соперника
                winner_move = get_winner(my_move, rival_move);
                print_output("Бой %d vs %d (раунд %d): %s vs %s => ",
                    fighter_id, rival_id, duel_rounds,
                    gesture_name(my_move), gesture_name(rival_move));
                
                if (winner_move == my_move) {  // текущий боец победил
                    print_output("Победил Боец %d\n", fighter_id);
                    arena.fighters[fighter_id].victories++;
                    arena.fighters[rival_id].active = 0;  // соперник выбывает
                    arena.alive_count--;
                    arena.fighters[fighter_id].has_rival = 0;
                    arena.fighters[fighter_id].rival_id = -1;
                    arena.fighters[rival_id].has_rival = 0;
                    arena.fighters[rival_id].rival_id = -1;
                    semaphore_post(&arena.arena_sem);
                } else if (winner_move == rival_move) {  // соперник победил
                    print_output("Победил Боец %d\n", rival_id);
                    arena.fighters[rival_id].victories++;
                    arena.fighters[fighter_id].active = 0;  // текущий боец выбывает
                    arena.alive_count--;
                    arena.fighters[fighter_id].has_rival = 0;
                    arena.fighters[fighter_id].rival_id = -1;
                    arena.fighters[rival_id].has_rival = 0;
                    arena.fighters[rival_id].rival_id = -1;
                    semaphore_post(&arena.arena_sem);
                } else {  // Ничья
                    print_output("Ничья\n");
                    semaphore_post(&arena.arena_sem);
                    usleep(300000);  // пауза перед следующим раундом
                    semaphore_wait(&arena.arena_sem);  // захват семафора для следующей итерации
                }
            } while (winner_move == (HandSign)-1);  // повторять пока ничья
        } else {  // у бойца нет соперника => ждем начала раунда
            semaphore_post(&arena.arena_sem);
            
            // ожидание сигнала о начале раунда с таймаутом 1 с
            pthread_mutex_lock(&arena.round_mutex);
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            int wait_result = pthread_cond_timedwait(&arena.round_cond, &arena.round_mutex, &ts);
            pthread_mutex_unlock(&arena.round_mutex);
            
            if (wait_result == ETIMEDOUT) {  // таймаут ожидания
                semaphore_wait(&arena.arena_sem);
                if (arena.finished) {
                    semaphore_post(&arena.arena_sem);
                    break;
                }
                semaphore_post(&arena.arena_sem);
            }
        }
        usleep(10000);  // пауза чтобы не было busy wait
    }
    
    print_output("Боец %d завершил участие.\n", fighter_id);
    return NULL;
}

// вывод списка активных бойцов
void print_active_fighters() {
    semaphore_wait(&arena.arena_sem);
    print_output("\nПромежуточные победители: ");
    int first = 1;
    for (int i = 0; i < arena.total_count; i++) {
        if (arena.fighters[i].active) {
            if (!first) {
                print_output(", ");
            }
            print_output("Боец %d", i);
            first = 0;
        }
    }
    print_output("\n");
    semaphore_post(&arena.arena_sem);
}

// очистка ресурсов программы
void cleanup() {
    print_output("Очистка ресурсов.\n");
    semaphore_wait(&arena.arena_sem);
    arena.finished = 1;  // установка флага завершения
    semaphore_post(&arena.arena_sem);
    
    // оповещение всех ожидающих потоков
    pthread_mutex_lock(&arena.round_mutex);
    pthread_cond_broadcast(&arena.round_cond);
    pthread_mutex_unlock(&arena.round_mutex);
    
    // лжидание завершения всех потоков
    for (int i = 0; i < fighter_count; i++) {
        if (fighter_threads[i]) {
            pthread_join(fighter_threads[i], NULL);
        }
    }
    
    // уничтожение синхропримитивов
    sem_destroy(&arena.arena_sem);
    pthread_mutex_destroy(&arena.round_mutex);
    pthread_cond_destroy(&arena.round_cond);
    
    if (output_file) {
        fclose(output_file);
        output_file = NULL;
    }
}

// обработчик сигналов прерывания
void signal_handler(int sig) {
    print_output("\nТурнир остановлен по сигналу %d.\n", sig);
    cleanup();
    exit(0);
}

// главная функция
int main(int argc, char *argv[]) {
    char* config_file = NULL;  // имя файла конфигурации
    char* output_filename = NULL;  // имя файла для вывода
    int read_from_file = 0; // флаг чтения из файла
    int custom_seed = 0; // пользовательский seed
    int use_custom_seed = 0; // флаг использования пользовательского seed
    
    // режим интерактивного ввода (при запуске без аргументов)
    if (argc == 1) {
        char input[100];
        printf("--- Турнир \"Камень-Ножницы-Бумага\" (version_4_8) ---\n");
        printf("Введите количество бойцов (2-32): ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("Ошибка чтения ввода\n");
            return 1;
        }
        fighter_count = atoi(input);
        
        printf("Вывести результаты в файл? (y/n): ");
        fgets(input, sizeof(input), stdin);
        if (input[0] == 'y' || input[0] == 'Y') {
            printf("Введите имя файла для вывода: ");
            fgets(input, sizeof(input), stdin);
            input[strcspn(input, "\n")] = 0;
            output_filename = strdup(input);
            if (!output_filename) {  // проверка успешности выделения памяти
                printf("Ошибка выделения памяти для имени файла\n");
                return 1;
            }
            use_file_output = 1;
        }
    } else {  // режим работы с аргументами командной строки
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
                config_file = argv[i + 1];  // чтение из файла конфигурации
                read_from_file = 1;
                i++;
            } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                output_filename = argv[i + 1];  // имя файла для вывода
                use_file_output = 1;
                i++;
            } else if (strcmp(argv[i], "-seed") == 0 && i + 1 < argc) {
                custom_seed = atoi(argv[i + 1]);  // пользоватедьский seed
                use_custom_seed = 1;
                i++;
            } else {
                // прямое указание количества бойцов
                char* endptr;
                fighter_count = strtol(argv[i], &endptr, 10);
                if (*endptr != '\0' || endptr == argv[i]) {
                    printf("Некорректный аргумент: %s\n", argv[i]);
                    return 1;
                }
            }
        }
    }
    
    // чтение количества бойцов из файла конфигурации
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
    }
    
    // проверка корректности количества бойцов
    if (fighter_count == 0) {
        printf("Количество бойцов не может быть 0\n");
        return 1;
    }
    
    if (fighter_count < 2 || fighter_count > MAX_FIGHTERS) {
        printf("Количество бойцов должно быть от 2 до %d\n", MAX_FIGHTERS);
        return 1;
    }
    
    // открытие файла для вывода результатов
    if (use_file_output && output_filename) {
        output_file = fopen(output_filename, "w");
        if (!output_file) {
            perror("Ошибка открытия файла для вывода");
            return 1;
        }
        printf("Вывод будет сохранен в файл: %s\n", output_filename);
    }
    
    // установка обработчиков сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // инициализация генератора случайных чисел
    if (use_custom_seed) {
        srand(custom_seed);
        print_output("Используется фиксированный seed: %d\n", custom_seed);
    } else {
        srand(time(NULL));
    }
    
    print_output("--- Турнир \"Камень-Ножницы-Бумага\" (version_4_8) ---\n");
    print_output("Количество участников: %d\n", fighter_count);
    
    // инициализация арены
    memset(&arena, 0, sizeof(Arena));
    arena.total_count = fighter_count;
    arena.alive_count = fighter_count;
    sem_init(&arena.arena_sem, 0, 1);  // инициализация семафора (нач значение 1)
    pthread_mutex_init(&arena.round_mutex, NULL);
    pthread_cond_init(&arena.round_cond, NULL);
    
    // инициализация бойцов
    for (int i = 0; i < fighter_count; i++) {
        arena.fighters[i].id = i;
        arena.fighters[i].active = 1;
        arena.fighters[i].victories = 0;
        arena.fighters[i].gesture = ROCK;
        arena.fighters[i].has_rival = 0;
        arena.fighters[i].rival_id = -1;
    }
    
    // создание потоков-бойцов
    print_output("Создание потоков-бойцов...\n");
    for (int i = 0; i < fighter_count; i++) {
        int* fighter_id = malloc(sizeof(int));
        *fighter_id = i;
        if (pthread_create(&fighter_threads[i], NULL, fighter_thread, fighter_id) != 0) {
            perror("Ошибка создания потока");
            free(fighter_id);
            cleanup();
            return 1;
        }
    }
    
    sleep(2);  // пауза для инициализации всех потоков
    print_output("\n------ Турнир начинается! ------\n");
    
    // главный цикл турнира
    int round = 0;
    while (!arena.finished) {
        semaphore_wait(&arena.arena_sem);
        int active = arena.alive_count;
        semaphore_post(&arena.arena_sem);
        
        if (active <= 1) {  // остался один боец => турнир завершен
            semaphore_wait(&arena.arena_sem);
            arena.finished = 1;
            semaphore_post(&arena.arena_sem);
            break;
        }
        
        print_output("\n--- Раунд %d ---\n", ++round);
        print_output("Активных бойцов: %d\n", active);
        
        setup_round();  // организация раунда
        sleep(3);  // пауза для проведения боев
        
        // ожидание завершения всех боев текущего раунда
        int duels_active;
        int max_waits = 30;
        do {
            duels_active = 0;
            semaphore_wait(&arena.arena_sem);
            for (int i = 0; i < fighter_count; i++) {
                if (arena.fighters[i].has_rival) {
                    duels_active = 1;
                    break;
                }
            }
            semaphore_post(&arena.arena_sem);
            if (duels_active) {
                sleep(1);
                max_waits--;
            }
        } while (duels_active && max_waits > 0);
        
        print_active_fighters();  // вывод промежуточных результатов
    }
    
    // определение победителя
    semaphore_wait(&arena.arena_sem);
    int winner_found = 0;
    for (int i = 0; i < fighter_count; i++) {
        if (arena.fighters[i].active) {
            print_output("\nТурнир завершен! Победитель: Боец %d\n", i);
            winner_found = 1;
            break;
        }
    }
    if (!winner_found) {
        print_output("\nТурнир завершен! Победитель не определен.\n");
    }
    semaphore_post(&arena.arena_sem);
    
    print_output("Все бои завершены.\n");
    cleanup();  // очистка ресурсов
    
    return 0;
}
