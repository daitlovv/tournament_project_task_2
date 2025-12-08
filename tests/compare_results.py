#!/usr/bin/env python3
"""
Скрипт для сравнения результатов двух версий программы
"""

import sys
import os

def compare_files(file1, file2, description):
    """Сравнивает два файла и выводит различия"""
    print(f"\n{description}:")

    if not os.path.exists(file1):
        print(f"  Файл {file1} не найден")
        return False
    if not os.path.exists(file2):
        print(f"  Файл {file2} не найден")
        return False

    with open(file1, 'r', encoding='utf-8') as f1, open(file2, 'r', encoding='utf-8') as f2:
        lines1 = f1.readlines()
        lines2 = f2.readlines()

    # Убираем строки с ID потоков (они всегда разные)
    filtered1 = [line for line in lines1 if 'Поток' not in line]
    filtered2 = [line for line in lines2 if 'Поток' not in line]

    if len(filtered1) != len(filtered2):
        print(f"  Разное количество строк: {len(filtered1)} vs {len(filtered2)}")
        return False

    differences = []
    for i, (line1, line2) in enumerate(zip(filtered1, filtered2), 1):
        if line1.strip() != line2.strip():
            differences.append((i, line1.strip(), line2.strip()))

    if differences:
        print(f"  Найдено {len(differences)} различий:")
        for diff in differences[:5]:  # Показываем только первые 5 различий
            print(f"    Строка {diff[0]}:")
            print(f"      Версия 1: {diff[1]}")
            print(f"      Версия 2: {diff[2]}")
        if len(differences) > 5:
            print(f"    ... и еще {len(differences) - 5} различий")
        return False
    else:
        print("  Файлы идентичны (игнорируя ID потоков)")
        return True

def main():
    print("=== Сравнение результатов двух версий программы ===")

    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # Сравниваем результаты для 8 бойцов
    file1 = os.path.join(base_dir, "version_4_8", "build", "results_mutex_8.txt")
    file2 = os.path.join(base_dir, "version_9_10", "build", "results_atomic_8.txt")
    compare_files(file1, file2, "Сравнение для 8 бойцов")

    # Сравниваем результаты для 16 бойцов
    file1 = os.path.join(base_dir, "version_4_8", "build", "results_mutex_16.txt")
    file2 = os.path.join(base_dir, "version_9_10", "build", "results_atomic_16.txt")
    compare_files(file1, file2, "Сравнение для 16 бойцов")

    print("\n=== Итог ===")
    print("Ожидаемые небольшие различия из-за:")
    print("1. Разных ID потоков")
    print("2. Разного порядка выполнения потоков")
    print("3. Разных синхропримитивов")
    print("\nОсновная логика (бойцы, победители, раунды) должна совпадать!")

if __name__ == "__main__":
    main()