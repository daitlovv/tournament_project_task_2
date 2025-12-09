#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}--- Запуск тестов многопоточного турнира ---${NC}"
echo ""

check_exit_code() {
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Успешно${NC}"
    else
        echo -e "${RED}Возникла ошибка${NC}"
    fi
}

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "Рабочая директория: $BASE_DIR"
cd "$BASE_DIR"

echo -e "${BLUE}1. Компиляция программ...${NC}"
echo ""

echo "Компиляция version_4_8"
if [ -d "version_4_8" ]; then
    cd version_4_8
    mkdir -p build
    cd build
    cmake ..
    make
    cd "$BASE_DIR"
    check_exit_code
else
    echo -e "${RED}Папка version_4_8 не найдена${NC}"
fi

echo ""
echo "Компиляция version_9_10"
if [ -d "version_9_10" ]; then
    cd version_9_10
    mkdir -p build
    cd build
    cmake ..
    make
    cd "$BASE_DIR"
    check_exit_code
else
    echo -e "${RED}Папка version_9_10 не найдена${NC}"
fi

echo ""
echo -e "${BLUE}2. Тестирование version_4_8...${NC}"
echo ""

if [ -f "version_4_8/build/tournament" ]; then
    cd version_4_8/build

    echo "Тест 1 (корректный, 8 бойцов, из файла)"
    if [ -f "../test_8_correct.txt" ]; then
        ./tournament -f ../test_8_correct.txt -o results_4_8_8.txt
        check_exit_code
    else
        echo -e "${RED}Файл test_8_correct.txt не найден${NC}"
    fi

    echo ""
    echo "Тест 2 (корректный, 16 бойцов, из командной строки)"
    ./tournament 16 -o results_4_8_16.txt
    check_exit_code

    echo ""
    echo "Тест 3 (некорректный, -3 бойца, из командной строки)"
    echo "Ожидается сообщение об ошибке:"
    ./tournament -3 2>&1 | tee error_4_8_-3.txt | head -5
    check_exit_code

    echo ""
    echo "Тест 4 (некорректный, 'fwv', из файла)"
    if [ -f "../test_letters_incorrect.txt" ]; then
        echo "Ожидается сообщение об ошибке:"
        ./tournament -f ../test_letters_incorrect.txt 2>&1 | tee error_4_8_letters.txt | head -5
        check_exit_code
    else
        echo -e "${RED}Файл test_letters_incorrect.txt не найден${NC}"
    fi

    cd "$BASE_DIR"
else
    echo -e "${RED}Файл version_4_8/build/tournament не найден${NC}"
fi

echo ""
echo -e "${BLUE}3. Тестирование version_9_10...${NC}"
echo ""

if [ -f "version_9_10/build/tournament" ]; then
    cd version_9_10/build

    echo "Тест 1 (некорректный, 0 бойцов, из файла)"
    if [ -f "../test_0_incorrect.txt" ]; then
        echo "Ожидается сообщение об ошибке:"
        ./tournament -f ../test_0_incorrect.txt 2>&1 | tee error_9_10_0.txt | head -5
        check_exit_code
    else
        echo -e "${RED}Файл test_0_incorrect.txt не найден${NC}"
    fi

    echo ""
    echo "Тест 2 (корректный, 4 бойца, из командной строки)"
    ./tournament 4 -o results_9_10_4.txt
    check_exit_code

    echo ""
    echo "Тест 3 (корректный, 32 бойца, из файла)"
    if [ -f "../test_32_correct.txt" ]; then
        ./tournament -f ../test_32_correct.txt -o results_9_10_32.txt
        check_exit_code
    else
        echo -e "${RED}Файл test_32_correct.txt не найден${NC}"
    fi

    echo ""
    echo "Тест 4 (некорректный, 50 бойцов, из командной строки)"
    echo "Ожидается сообщение об ошибке:"
    ./tournament 50 2>&1 | tee error_9_10_50.txt | head -5
    check_exit_code

    cd "$BASE_DIR"
else
    echo -e "${RED}Файл version_9_10/build/tournament не найден${NC}"
fi

echo ""
echo -e "${GREEN}--- Тестирование завершено ---${NC}"
echo "Результаты соответствующих турниров находятся в:"
echo "- version_4_8/build/results_4_8_8.txt"
echo "- version_4_8/build/results_4_8_16.txt"
echo "- version_4_8/build/error_4_8_-3.txt"
echo "- version_4_8/build/error_4_8_letters.txt"
echo "- version_9_10/build/results_9_10_4.txt"
echo "- version_9_10/build/results_9_10_32.txt"
echo "- version_9_10/build/error_9_10_0.txt"
echo "- version_9_10/build/error_9_10_50.txt"
