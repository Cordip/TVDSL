# HW2: парсер Funny (LL(1), рекурсивный спуск) и AST

## Сборка и запуск

Нужны `make`, `gcc` или `clang` и соседняя папка `../hw1` (лексер собирается из её исходников).
Для проверок нужен Python 3.

```sh
make
./hw2 example.funny                     # AST деревом в терминале
./hw2 --raw example.funny               # дерево до обессахаривания
./hw2 --dot example.funny > ast.dot     # граф в DOT
./hw2 --raw --dot example.funny > raw.dot # DOT до обессахаривания
make build/api-example                 # пример потребителя C API
./build/api-example < example.funny     # обход функций и печать AST
```

Ошибки печатаются в stderr как `файл:строка:столбец: error: сообщение`.
Код выхода: 0 — разобрано, 1 — ошибка чтения, лексики, синтаксиса или вывода, 2 — неверные аргументы.
При лексической или синтаксической ошибке AST не печатается.

## Тесты

```sh
make test              # 163 CLI-проверки, 35 C-проверок, 40 Python-тестов, 26159 испорченных входов
make check             # то же под ASan/UBSan
make ll1               # FIRST/FOLLOW и проверка конфликтов LL(1)
UPDATE=1 make test      # перезаписывает ожидаемые результаты
make clean             # удаляет hw2 и build/
```

Примеры курса лежат в `tests/examples/`; другой путь: `make test EXAMPLES=путь`.

## Результаты

- Грамматика, приоритеты, FIRST/FOLLOW, обработка ошибок, отклонения от `funny.ru.md`: `grammar.md`.
- API, формат AST и подключение к T0: [API.md](API.md); объявления: `include/hw2.h`.
- Пример подключения: `examples/embed.c`.
- Тесты: `tests/cases/*.funny`, ожидаемый результат рядом: `.tree`, `.raw.tree`, `.dot`, `.raw.dot` или `.err`.
- Компонентные тесты C: `tests/unit.c`; тесты грамматики и runner: `tests/test_*.py`.
- Примеры курса: `tests/examples/`; ожидаемый результат для каждого: `tests/examples.expected`.
