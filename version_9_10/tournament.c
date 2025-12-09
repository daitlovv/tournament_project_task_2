#define _GNU_SOURCE
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
#include <stdarg.h>

#define MAX_FIGHTERS 32

// перечисление для жестов "Камень-ножницы-бумага"
typedef enum {
    ROCK = 0,
    SCISSORS = 1,
    PAPER = 2
} HandSign;

// структура бойца с атомарными переменными
typedef struct {
    int id;
    atomic_int active;  // атомарный флаг активности бойца
    atomic_int victories;  // атомарный счетчик побед
    atomic_int has_rival;  // атомарный флаг наличия соперника
    atomic_int rival_id; // атомарный ID соперника
    pthread_t thread_id; // идентификатор потока
} Combatant;

// арена турнира
typedef struct {
    Combatant fighters[MAX_FIGHTERS];  // массив бойцов
    int total_count; // общее количество бойцов
    atomic_int alive_count;  // атомарный счетчик живых бойцов
    atomic_int round_num;  // атомарный номер текущего раунда
    atomic_int finished; // атомарный флаг завершения турнира
    pthread_spinlock_t arena_spinlock; // спинлок для защиты критических секций
    atomic_int round_started; // атомарный флаг начала раунда
} Arena;

Arena arena;
int fighter_count;
pthread_t fighter_threads[MAX_FIGHTERS];
FILE* output_file = NULL;
int use_file_output = 0;

// универсальная функция вывода (консоль + файл)
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

// функция определения победителя в бою
HandSign get_winner(HandSign sign1, HandSign sign2) {
    if (sign1 == sign2) {
        return (HandSign)-1;  // ничья
    }
    if ((sign1 == ROCK && sign2 == SCISSORS) ||
        (sign1 == SCISSORS && sign2 == PAPER) ||
        (sign1 == PAPER && sign2 == ROCK)) {
        return sign1;
    }
    return sign2;
}

// функция преобразования жеста в строку
const char* gesture_name(HandSign sign) {
    switch(sign) {
        case ROCK: return "Камень";
        case SCISSORS: return "Ножницы";
        case PAPER: return "Бумага";
        default: return "Неизвестно";
    }
}

// функция организации раунда
void setup_round() {
    pthread_spin_lock(&arena.arena_spinlock);  // захват спинлока
    
    if (atomic_load(&arena.finished)) {
        pthread_spin_unlock(&arena.arena_spinlock);
        return;
    }
    
    atomic_store(&arena.round_started, 1);  // устанавливаем флаг начала раунда
    
    // сбор активных бойцов без соперника
    int ready_fighters[MAX_FIGHTERS];
    int count = 0;
    for (int i = 0; i < arena.total_count; i++) {
        if (atomic_load(&arena.fighters[i].active) &&
            !atomic_load(&arena.fighters[i].has_rival)) {
            ready_fighters[count++] = i;
        }
    }
    
    // случайное перемешивание бойцов
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
        
        // атомарная установка флагов соперничества
        atomic_store(&arena.fighters[fighter1].has_rival, 1);
        atomic_store(&arena.fighters[fighter1].rival_id, fighter2);
        atomic_store(&arena.fighters[fighter2].has_rival, 1);
        atomic_store(&arena.fighters[fighter2].rival_id, fighter1);
        
        print_output("Организован бой: Боец %d vs Боец %d\n", fighter1, fighter2);
    }
    
    atomic_fetch_add(&arena.round_num, 1);  // атомарное увеличение номера раунда
    print_output("Начало раунда %d. Бойцов готово к бою: %d\n",
                 atomic_load(&arena.round_num), count);
    
    pthread_spin_unlock(&arena.arena_spinlock);  // освобождение спинлока
}

// функция потока-бойца
void* fighter_thread(void* arg) {
    int fighter_id = *(int*)arg;
    free(arg);
    
    // инициализация уникального seed для генератора случайных чисел
    unsigned int seed = time(NULL) + fighter_id + pthread_self();
    print_output("Боец %d (Поток %lu) начал участие в турнире.\n",
                 fighter_id, (unsigned long)pthread_self());
    
    while (1) {
        if (atomic_load(&arena.finished)) {
            break;
        }
        
        if (!atomic_load(&arena.fighters[fighter_id].active)) {
            usleep(10000);
            continue;
        }
        
        // ожидание начала раунда с timeout
        int timeout_counter = 10000;
        while (atomic_load(&arena.round_started) == 0 && 
            !atomic_load(&arena.finished) && timeout_counter > 0) {
            usleep(1000);
            timeout_counter--;
        }
        
        if (timeout_counter <= 0 && atomic_load(&arena.round_started) == 0) {
            print_output("Боец %d: timeout ожидания раунда\n", fighter_id);
            break;
        }
        
        if (atomic_load(&arena.finished)) {
            break;
        }
        
        // если у бойца есть соперник
        if (atomic_load(&arena.fighters[fighter_id].has_rival)) {
            int rival_id = atomic_load(&arena.fighters[fighter_id].rival_id);
            
            // проверка корректности соперника
            if (rival_id < 0 || rival_id >= arena.total_count ||
                !atomic_load(&arena.fighters[rival_id].active)) {
                atomic_store(&arena.fighters[fighter_id].has_rival, 0);
                atomic_store(&arena.fighters[fighter_id].rival_id, -1);
                continue;
            }
            
            // гарантируем, что бой проводит боец с меньшим ID
            if (fighter_id > rival_id) {
                atomic_store(&arena.fighters[fighter_id].has_rival, 0);
                atomic_store(&arena.fighters[fighter_id].rival_id, -1);
                continue;
            }
            
            HandSign my_move;
            HandSign rival_move;
            HandSign winner_move;
            int duel_rounds = 0;
            
            // цикл боя (повторяется при ничьей)
            do {
                duel_rounds++;
                my_move = rand_r(&seed) % 3;  // генерация жеста бойца
                rival_move = rand_r(&seed) % 3;   // генерация жеста соперника
                winner_move = get_winner(my_move, rival_move);
                
                print_output("Бой %d vs %d (раунд %d): %s vs %s => ",
                    fighter_id, rival_id, duel_rounds,
                    gesture_name(my_move), gesture_name(rival_move));
                
                if (winner_move == my_move) {
                    print_output("Победил Боец %d\n", fighter_id);
                    // атомарные операции обновления состояния
                    atomic_fetch_add(&arena.fighters[fighter_id].victories, 1);
                    atomic_store(&arena.fighters[rival_id].active, 0);
                    atomic_fetch_sub(&arena.alive_count, 1);
                } else if (winner_move == rival_move) {
                    print_output("Победил Боец %d\n", rival_id);
                    // атомарные операции обновления состояния
                    atomic_fetch_add(&arena.fighters[rival_id].victories, 1);
                    atomic_store(&arena.fighters[fighter_id].active, 0);
                    atomic_fetch_sub(&arena.alive_count, 1);
                } else {
                    print_output("Ничья\n");
                    usleep(300000);  // пауза перед следующим раундом боя
                }
            } while (winner_move == (HandSign)-1 &&
                     atomic_load(&arena.fighters[fighter_id].active) &&
                     atomic_load(&arena.fighters[rival_id].active));
            
            // сброс флагов соперничества после боя
            atomic_store(&arena.fighters[fighter_id].has_rival, 0);
            atomic_store(&arena.fighters[fighter_id].rival_id, -1);
            atomic_store(&arena.fighters[rival_id].has_rival, 0);
            atomic_store(&arena.fighters[rival_id].rival_id, -1);
        }
        
        usleep(10000);  // пауза для снижения нагрузки на CPU
    }
    
    print_output("Боец %d завершил участие.\n", fighter_id);
    return NULL;
}

// функция вывода списка активных бойцов
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

// функция очистки ресурсов
void cleanup() {
    print_output("Очистка ресурсов.\n");
    
    // установка флагов завершения
    atomic_store(&arena.finished, 1);
    atomic_store(&arena.round_started, 1);
    
    usleep(100000);  // пауза для завершения потоков
    
    // ожидание завершения всех потоков
    for (int i = 0; i < fighter_count; i++) {
        if (fighter_threads[i]) {
            pthread_join(fighter_threads[i], NULL);
        }
    }
    
    pthread_spin_destroy(&arena.arena_spinlock);  // уничтожение спинлока
    
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
    char* config_file = NULL;
    char* output_filename = NULL;
    int read_from_file = 0;
    int custom_seed = 0;
    int use_custom_seed = 0;
    
    // парсинг аргументов командной строки
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            config_file = argv[i + 1];
            read_from_file = 1;
            i++;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_filename = argv[i + 1];
            use_file_output = 1;
            i++;
        } else if (strcmp(argv[i], "-seed") == 0 && i + 1 < argc) {
            custom_seed = atoi(argv[i + 1]);
            use_custom_seed = 1;
            i++;
        } else {
            char* endptr;
            fighter_count = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || endptr == argv[i]) {
                printf("Некорректный аргумент: %s\n", argv[i]);
                return 1;
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
    } else if (fighter_count == 0) {
        return 1;
    }
    
    // проверка допустимого диапазона количества бойцов
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
    
    print_output("--- Турнир \"Камень-Ножницы-Бумага\" ---\n");
    print_output("Количество участников: %d\n", fighter_count);
    
    // инициализация арены турнира
    memset(&arena, 0, sizeof(Arena));
    arena.total_count = fighter_count;
    atomic_store(&arena.alive_count, fighter_count);
    atomic_store(&arena.round_num, 0);
    atomic_store(&arena.finished, 0);
    atomic_store(&arena.round_started, 0);
    pthread_spin_init(&arena.arena_spinlock, PTHREAD_PROCESS_PRIVATE);
    
    // инициализация бойцов
    for (int i = 0; i < fighter_count; i++) {
        arena.fighters[i].id = i;
        atomic_store(&arena.fighters[i].active, 1);
        atomic_store(&arena.fighters[i].victories, 0);
        atomic_store(&arena.fighters[i].has_rival, 0);
        atomic_store(&arena.fighters[i].rival_id, -1);
    }
    
    print_output("Создание потоков-бойцов...\n");
    
    // создание потоков-бойцов
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
    
    sleep(2);
    print_output("\n------ Турнир начинается! ------\n");
    
    // главный цикл
    int round = 0;
    while (!atomic_load(&arena.finished)) {
        int active = atomic_load(&arena.alive_count);
        
        if (active <= 1) {
            atomic_store(&arena.finished, 1);
            break;
        }
        
        print_output("\n--- Раунд %d ---\n", ++round);
        print_output("Активных бойцов: %d\n", active);
        
        atomic_store(&arena.round_started, 0); // сброс флага начала раунда
        setup_round();  // организация раунда
        sleep(2);  // пауза между раундами
        
        // ожидание завершения всех боев в раунде
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
        
        print_active_fighters();  // вывод промежуточных результатов
    }
    
    atomic_store(&arena.finished, 1);
    atomic_store(&arena.round_started, 1);
    
    // определение и вывод победителя
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
    cleanup();  // очистка ресурсов
    
    return 0;
}
