# HW1: лексер Funny (регулярные выражения → НКА → ДКА → минимальный ДКА)

## Сборка и запуск

Нужны `make` и `gcc` или `clang`.

```sh
make
./hw1 --report               # правила, размеры автоматов, ловушка, эквивалентность
./hw1 --export dfa.csv       # таблица переходов минимального ДКА
./hw1 --scan example.funny   # токены
```

## Тесты

```sh
make test             # 61 проверка
make check            # то же под ASan/UBSan
make extended         # test + сверка с <regex.h> (build/differential)
make check-extended   # check + сверка с <regex.h> под ASan/UBSan
make fuzz             # libFuzzer, 60 с, нужен clang
make clean            # удаляет hw1 и build/
```

## Результаты

- Регулярные выражения: `src/spec/rules.c`, список печатает `./hw1 --report`.
- Тесты, вход и ожидаемый результат: `tests/cases.h`.
- Состояний: НКА 301, ДКА 126, минимальный ДКА 123.
- Ловушка: состояние 50, одно на все переходы вне правил; не-ASCII байты допустимы только в комментариях.
- В `dfa.csv` 129 символов на состояние: ASCII `0..127` и `128` для любого байта вне ASCII.
