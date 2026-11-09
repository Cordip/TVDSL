# Грамматика Funny для LL(1)-парсера

Синтаксическая часть описана над токенами: ключевые слова и знаки — в кавычках, `IDENT` и `INT`
определены в лексической части ниже. Лексер HW1 выделяет эти токены из байтов исходника.
Парсер (`src/parse/parser.c`) — рекурсивный спуск, одна функция на нетерминал: `conditional` → `parse_conditional`.
Уровни выражений от `implication` до `unary` реализует одна функция `parse_expr` (precedence climbing) по таблице
приоритетов ниже.

## Грамматика

```ebnf
module       = item { item } ;
item         = "function" IDENT parameters functionTail
             | IDENT parameters ( "=>" predicate [ ";" ] | functionTail ) ;
parameters   = "(" [ variableDef { "," variableDef } ] ")" ;
functionTail = [ "requires" predicate ] "returns" variableDef { "," variableDef }
               [ "ensures" predicate ] [ "uses" localVarDef { "," localVarDef } ]
               statement ;
variableDef  = IDENT ":" variableType ;
localVarDef  = IDENT [ ":" variableType ] ;
variableType = "int" [ "[" "]" ] ;

statement    = assignment | conditional | loop | block | assertion ;
assignment   = IDENT ( "=" expr ";"
                     | index { index } "=" expr ";"
                     | "," IDENT { "," IDENT } "=" functionCall ";" ) ;
conditional  = "if" "(" condition ")" statement [ "else" statement ] ;
loop         = "while" "(" condition ")" [ "invariant" predicate ] statement ;
block        = "{" { statement } "}" ;
assertion    = ( "assert" | "assume" ) predicate ";" ;

condition    = expr ;
predicate    = expr ;
expr         = implication ;
implication  = disjunction [ "->" implication ] ;
disjunction  = conjunction { "or" conjunction } ;
conjunction  = negation { "and" negation } ;
negation     = "not" negation | comparison ;
comparison   = sum [ compareOp sum ] ;
compareOp    = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
sum          = term { ( "+" | "-" ) term } ;
term         = unary { ( "*" | "/" ) unary } ;
unary        = "-" unary | primary ;
primary      = INT | "true" | "false" | "(" expr ")" | quantifier
             | IDENT [ arguments | index { index } ] ;
quantifier   = ( "forall" | "exists" ) "(" variableDef "|" predicate ")" ;
functionCall = IDENT arguments ;
arguments    = "(" [ expr { "," expr } ] ")" ;
index        = "[" expr "]" ;
```

## Лексическая часть

Правила ниже задают лексемы над байтами исходного файла. `IDENT` и `INT` — экспортируемые в синтаксис
имена токенов; остальные имена — вспомогательные определения. Строковые терминалы задают указанные
ASCII-байты; `\t`, `\r`, `\n` обозначают табуляцию, CR и LF. Единственная специальная последовательность
в `commentByte` задаёт объединение трёх включительных диапазонов байтов: `0x00–0x09`, `0x0B–0x0C`,
`0x0E–0xFF`. Это точно все байты, кроме CR и LF, включая байты не-ASCII.

```ebnf-lexical
IDENT = identifier ;
INT = integer ;
identifier = ( letter | "_" ) { letter | digit | "_" } ;
integer = "0" | nonzeroDigit { digit } ;
invalidInteger = "0" digit { digit } ;
digit = "0" | nonzeroDigit ;
nonzeroDigit = "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
letter = "A" | "B" | "C" | "D" | "E" | "F" | "G" | "H" | "I" | "J" | "K" | "L" | "M"
       | "N" | "O" | "P" | "Q" | "R" | "S" | "T" | "U" | "V" | "W" | "X" | "Y" | "Z"
       | "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h" | "i" | "j" | "k" | "l" | "m"
       | "n" | "o" | "p" | "q" | "r" | "s" | "t" | "u" | "v" | "w" | "x" | "y" | "z" ;
whitespace = whitespaceByte { whitespaceByte } ;
whitespaceByte = " " | "\t" | "\r" | "\n" ;
comment = "//" { commentByte } ;
commentByte = ? byte 0x00-0x09, 0x0B-0x0C, 0x0E-0xFF ? ;
keyword = "function" | "returns" | "requires" | "ensures" | "uses"
        | "while" | "if" | "else" | "assert" | "assume" | "invariant"
        | "length" | "true" | "false" | "not" | "and" | "or"
        | "forall" | "exists" | "int" ;
delimiter = "(" | ")" | "{" | "}" | "[" | "]" | "," | ";" | ":" | "|" ;
operator = "+" | "-" | "*" | "/" | "==" | "!=" | "<=" | ">="
         | "<" | ">" | "=" | "->" | "=>" ;
```

Лексер выбирает самое длинное совпадение. При одинаковой длине выигрывает первое правило HW1:
пробелы, комментарии, ключевые слова, ошибочные целые, целые и идентификаторы, разделители, операторы.
Пробелы и комментарии пропускаются; `invalidInteger` даёт лексическую ошибку, поэтому `007`
не превращается в несколько целых. Ключевое слово выигрывает у совпавшего с ним идентификатора;
`ifx` остаётся идентификатором из-за большей длины. Адаптер HW2 переводит только ключевое слово
`length` обратно в `IDENT`, разрешая его как имя и обычный вызов. Байт не-ASCII вне комментария
не соответствует допустимой лексеме и даёт ошибку. Конец ввода — отдельный токен EOF с позицией
сразу после последнего байта; в FIRST/FOLLOW он обозначается `$`.

Все именованные ссылки проверяются в своей части грамматики. В синтаксическую часть
импортируются явно определённые лексические токены `IDENT` и `INT`. FIRST/FOLLOW считаются
для синтаксиса над токенами; лексические пересечения разрешаются правилами выше.

## Вид выражения

`expr`, `condition` и `predicate` разбирает один парсер выражений. Сразу после разбора подвыражения парсер сверяет
его вид с тем, что требует позиция, и сообщает, например, `expected condition, found expression`.

- Логические: `true`, `false`, сравнения, `not`, `and`, `or`, `->`, кванторы.
- Целые: `INT`, переменные, `a[i]`, арифметика, вызовы. `length` — обычный идентификатор; `length(a)` — вызов `Call "length"`.
- Вызов в `predicate` — ссылка на формулу там, где нужно логическое значение, и вызов функции там, где нужно целое.
- Операнды `not`, `and`, `or`, `->`, тело квантора, `condition` и `predicate` — логические; операнды арифметики и
  сравнений, индексы, аргументы вызовов и правая часть `=` — целые.
- Квантор допустим только внутри `predicate` (`requires`, `ensures`, `invariant`, `assert`, `assume`, тело формулы).

Префиксный `not` разбирается в любой позиции операнда, но операнды сильнее `not` целые, поэтому `a < not b` отвергается
проверкой вида: принимаемый язык тот же, что у грамматики.

## Приоритеты

| Уровень     | Операторы                      | Ассоциативность |
| ----------- | ------------------------------ | --------------- |
| 1 (слабее)  | `->`                           | правая          |
| 2           | `or`                           | левая           |
| 3           | `and`                          | левая           |
| 4           | `not` (префикс)                | —               |
| 5           | `==` `!=` `<` `<=` `>` `>=`    | нет: `a < b < c` — ошибка |
| 6           | `+` `-`                        | левая           |
| 7           | `*` `/`                        | левая           |
| 8 (сильнее) | `-` (унарный)                  | —               |

## FIRST и FOLLOW

Таблица вычисляется из EBNF командой `make ll1`. `ε` — пустая последовательность, `$` — конец ввода.
Терминалы без кавычек (`IDENT`, `INT`) должны быть определены в лексической части;
одного написания прописными буквами недостаточно для объявления токена.
Проверка также отвергает левую рекурсию и неизвестные нетерминалы.
Для обновления таблицы: `python3 tools/ll1.py --update grammar.md`.

<!-- ll1:begin -->
| Нетерминал | FIRST | FOLLOW |
| --- | --- | --- |
| `module` | `"function"`, `IDENT` | `$` |
| `item` | `"function"`, `IDENT` | `"function"`, `$`, `IDENT` |
| `parameters` | `"("` | `"=>"`, `"requires"`, `"returns"` |
| `functionTail` | `"requires"`, `"returns"` | `"function"`, `$`, `IDENT` |
| `variableDef` | `IDENT` | `")"`, `","`, `"assert"`, `"assume"`, `"ensures"`, `"if"`, `"uses"`, `"while"`, `"{"`, `"\|"`, `IDENT` |
| `localVarDef` | `IDENT` | `","`, `"assert"`, `"assume"`, `"if"`, `"while"`, `"{"`, `IDENT` |
| `variableType` | `"int"` | `")"`, `","`, `"assert"`, `"assume"`, `"ensures"`, `"if"`, `"uses"`, `"while"`, `"{"`, `"\|"`, `IDENT` |
| `statement` | `"assert"`, `"assume"`, `"if"`, `"while"`, `"{"`, `IDENT` | `"assert"`, `"assume"`, `"else"`, `"function"`, `"if"`, `"while"`, `"{"`, `"}"`, `$`, `IDENT` |
| `assignment` | `IDENT` | `"assert"`, `"assume"`, `"else"`, `"function"`, `"if"`, `"while"`, `"{"`, `"}"`, `$`, `IDENT` |
| `conditional` | `"if"` | `"assert"`, `"assume"`, `"else"`, `"function"`, `"if"`, `"while"`, `"{"`, `"}"`, `$`, `IDENT` |
| `loop` | `"while"` | `"assert"`, `"assume"`, `"else"`, `"function"`, `"if"`, `"while"`, `"{"`, `"}"`, `$`, `IDENT` |
| `block` | `"{"` | `"assert"`, `"assume"`, `"else"`, `"function"`, `"if"`, `"while"`, `"{"`, `"}"`, `$`, `IDENT` |
| `assertion` | `"assert"`, `"assume"` | `"assert"`, `"assume"`, `"else"`, `"function"`, `"if"`, `"while"`, `"{"`, `"}"`, `$`, `IDENT` |
| `condition` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"not"`, `"true"`, `IDENT`, `INT` | `")"` |
| `predicate` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"not"`, `"true"`, `IDENT`, `INT` | `")"`, `";"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `expr` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"not"`, `"true"`, `IDENT`, `INT` | `")"`, `","`, `";"`, `"]"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `implication` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"not"`, `"true"`, `IDENT`, `INT` | `")"`, `","`, `";"`, `"]"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `disjunction` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"not"`, `"true"`, `IDENT`, `INT` | `")"`, `","`, `"->"`, `";"`, `"]"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `conjunction` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"not"`, `"true"`, `IDENT`, `INT` | `")"`, `","`, `"->"`, `";"`, `"]"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `negation` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"not"`, `"true"`, `IDENT`, `INT` | `")"`, `","`, `"->"`, `";"`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `comparison` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"true"`, `IDENT`, `INT` | `")"`, `","`, `"->"`, `";"`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `compareOp` | `"!="`, `"<"`, `"<="`, `"=="`, `">"`, `">="` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"true"`, `IDENT`, `INT` |
| `sum` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"true"`, `IDENT`, `INT` | `"!="`, `")"`, `","`, `"->"`, `";"`, `"<"`, `"<="`, `"=="`, `">"`, `">="`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `term` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"true"`, `IDENT`, `INT` | `"!="`, `")"`, `"+"`, `","`, `"-"`, `"->"`, `";"`, `"<"`, `"<="`, `"=="`, `">"`, `">="`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `unary` | `"("`, `"-"`, `"exists"`, `"false"`, `"forall"`, `"true"`, `IDENT`, `INT` | `"!="`, `")"`, `"*"`, `"+"`, `","`, `"-"`, `"->"`, `"/"`, `";"`, `"<"`, `"<="`, `"=="`, `">"`, `">="`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `primary` | `"("`, `"exists"`, `"false"`, `"forall"`, `"true"`, `IDENT`, `INT` | `"!="`, `")"`, `"*"`, `"+"`, `","`, `"-"`, `"->"`, `"/"`, `";"`, `"<"`, `"<="`, `"=="`, `">"`, `">="`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `quantifier` | `"exists"`, `"forall"` | `"!="`, `")"`, `"*"`, `"+"`, `","`, `"-"`, `"->"`, `"/"`, `";"`, `"<"`, `"<="`, `"=="`, `">"`, `">="`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `functionCall` | `IDENT` | `";"` |
| `arguments` | `"("` | `"!="`, `")"`, `"*"`, `"+"`, `","`, `"-"`, `"->"`, `"/"`, `";"`, `"<"`, `"<="`, `"=="`, `">"`, `">="`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
| `index` | `"["` | `"!="`, `")"`, `"*"`, `"+"`, `","`, `"-"`, `"->"`, `"/"`, `";"`, `"<"`, `"<="`, `"="`, `"=="`, `">"`, `">="`, `"["`, `"]"`, `"and"`, `"assert"`, `"assume"`, `"function"`, `"if"`, `"or"`, `"returns"`, `"uses"`, `"while"`, `"{"`, `$`, `IDENT` |
<!-- ll1:end -->

Единственный конфликт LL(1) — висячий `else`: `"else"` входит и в FIRST необязательной части `conditional`, и в
FOLLOW(`statement`). Парсер выбирает `else`, то есть `else` относится к ближайшему `if`.

## Ошибки

- Формат: `файл:строка:столбец: error: сообщение`; лексические ошибки в том же формате, после первой разбор не идёт.
- Режим паники: ошибка в операторе блока пропускает токены до `;` (включительно), до сбалансированного `{...}` или до
  `}` блока; если сразу после пропущенной ветки идёт `else`, пропускается и его ветка. Ошибка в функции или формуле —
  до начала следующей функции или формулы (`"function"` или
  `IDENT ( ... )` перед `requires`, `returns`, `=>`) или до конца файла. Если правило не съело ни одного токена,
  сначала пропускается один токен, поэтому разбор всегда завершается.
- Вложенность разбора и высота AST операторов и выражений ограничены 1000 уровнями. Высота учитывает цепочки бинарных
  операций, вызовы, индексы и дерево после обессахаривания. Превышение: `nesting deeper than 1000 levels`.
- Незакрытый блок сообщает `expected '}' to close '{' at строка:столбец` с позицией своей `{`.

## Отклонения от funny.ru.md

- Перед именем функции (не формулы) допускается ключевое слово `function`: `function f(x: int) returns r: int ...`.
- `->` допустим в предикатах, а не только в условиях.
- Цепочки обращений `a[i][j]` разбираются, в том числе слева от `=`; отвергать их должны типы.
- `→` (U+2192) не поддерживается: лексер HW1 отвергает этот символ, вместо него пишется `->`.
- Идентификаторы начинаются с буквы или `_`, затем содержат буквы, цифры или `_` (правило лексера HW1).
- `;` после формулы необязательна: в funny.ru.md `;` правила `formula` — терминатор EBNF, а примеры курса её пишут.
  Принимаются `positive(x: int) => x > 0;` и `positive(x: int) => x > 0`.
