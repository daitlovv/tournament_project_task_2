#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}=== Запуск тестов многопоточного турнира ===${NC}"
echo ""

check_exit_code() {
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Успех${NC}"
    else
        echo -e "${RED}✗ Ошибка${NC}"
    fi
}

echo -e "${YELLOW}1. Компиляция программ...${NC}"
echo ""

echo "Компиляция version_4_8..."
cd version_4_8
mkdir -p build
cd build
cmake ..
make
cd ../..
check_exit_code

echo ""
echo "Компиляция version_9_10..."
cd version_9_10
mkdir -p build
cd build
cmake ..
make
cd ../..
check_exit_code

echo ""
echo -e "${YELLOW}2. Тестирование версии с мьютексами (version_4_8)...${NC}"
echo ""

cd version_4_8/build
echo "Тест 1: 4 бойца (командная строка)"
./tournament 4 2>&1 | tail -10
check_exit_code

echo ""
echo "Тест 2: 8 бойцов (из файла)"
./tournament -f ../test_8_correct.txt -o results_mutex_8.txt
check_exit_code
echo "Результаты: results_mutex_8.txt"

echo ""
echo "Тест 3: 16 бойцов (из файла)"
./tournament -f ../test_16_correct.txt -o results_mutex_16.txt
check_exit_code
echo "Результаты: results_mutex_16.txt"

echo ""
echo "Тест 4: 32 бойца (максимум)"
./tournament -f ../test_32_correct.txt -o results_mutex_32.txt
check_exit_code
echo "Результаты: results_mutex_32.txt"

echo ""
echo "Тест 5: Проверка ошибок - 0 бойцов"
./tournament -f ../test_0_incorrect.txt 2>&1 | grep -E "(Ошибка|Error|Invalid|использовано)" || true
check_exit_code

echo ""
echo "Тест 6: Проверка ошибок - 50 бойцов"
./tournament -f ../test_50_incorrect.txt 2>&1 | grep -E "(Ошибка|Error|Invalid|использовано)" || true
check_exit_code

echo ""
echo "Тест 7: Проверка ошибок - буквы"
./tournament -f ../test_letters_incorrect.txt 2>&1 | grep -E "(Ошибка|Error|Invalid|использовано)" || true
check_exit_code

cd ../..

echo ""
echo -e "${YELLOW}3. Тестирование атомарной версии (version_9_10)...${NC}"
echo ""

cd version_9_10/build
echo "Тест 1: 4 бойца (командная строка)"
./tournament 4 2>&1 | tail -10
check_exit_code

echo ""
echo "Тест 2: 8 бойцов (из файла)"
./tournament -f ../test_8_correct.txt -o results_atomic_8.txt
check_exit_code
echo "Результаты: results_atomic_8.txt"

echo ""
echo "Тест 3: 16 бойцов (из файла)"
./tournament -f ../test_16_correct.txt -o results_atomic_16.txt
check_exit_code
echo "Результаты: results_atomic_16.txt"

echo ""
echo "Тест 4: 32 бойца (максимум)"
./tournament -f ../test_32_correct.txt -o results_atomic_32.txt
check_exit_code
echo "Результаты: results_atomic_32.txt"

cd ../..

echo ""
echo -e "${YELLOW}4. Сравнение результатов...${NC}"
echo ""

if command -v python3 &> /dev/null; then
    python3 tests/compare_results.py
elif command -v diff &> /dev/null; then
    echo "Сравнение результатов для 8 бойцов:"
    diff version_4_8/build/results_mutex_8.txt version_9_10/build/results_atomic_8.txt 2>/dev/null || echo "Файлы идентичны или один отсутствует"
    echo ""
    echo "Сравнение результатов для 16 бойцов:"
    diff version_4_8/build/results_mutex_16.txt version_9_10/build/results_atomic_16.txt 2>/dev/null || echo "Файлы идентичны или один отсутствует"
else
    echo "Для сравнения результатов установите python3 или diff"
fi

echo ""
echo -e "${GREEN}=== Тестирование завершено ===${NC}"
echo "Результаты находятся в:"
echo "- version_4_8/build/results_*.txt"
echo "- version_9_10/build/results_*.txt"