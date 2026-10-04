# Seng Standard Library API Reference

Seng comes with a set of built-in packages to handle everything from math to HTTP requests.

## 🧮 `math` Package
High-performance mathematical operations.
- `sqrt(n)`: Returns the square root of `n`.
- `floor(n)`: Rounds `n` down to the nearest integer.
- `ceil(n)`: Rounds `n` up to the nearest integer.
- `round(n)`: Rounds `n` to the nearest integer.
- `abs(n)`: Returns the absolute value of `n`.
- `power(base, exp)`: Returns `base` raised to the power of `exp`.
- `sin(n)`, `cos(n)`, `tan(n)`: Trigonometric functions (radians).
- `min(a, b)`, `max(a, b)`: Returns the smaller or larger of two numbers.
- `random()`: Returns a random number between 0 and 1.
- `random_int(min, max)`: Returns a random integer between `min` and `max`.
- `pi`: Constant representing 3.14159...

## ⏱️ `time` Package
Time and date utilities.
- `now()`: Returns current Unix timestamp.
- `format_time(ts)`: Formats a Unix timestamp into a readable string (`YYYY-MM-DD HH:MM:SS`).

## 🔡 `string` Package
Comprehensive string manipulation.
- `str_len(s)`: Returns the length of string `s`.
- `upper(s)`, `lower(s)`: Converts string to uppercase or lowercase.
- `trim(s)`: Removes leading and trailing whitespace.
- `contains(s, sub)`: Returns true if `s` contains `sub`.
- `starts_with(s, pre)`, `ends_with(s, suf)`: Checks prefix/suffix.
- `replace(s, old, new)`: Replaces all occurrences of `old` with `new`.
- `split(s, sep)`: Splits `s` into a list of strings by `sep`.
- `join(list, sep)`: Joins a list of strings using `sep`.
- `str_num(s)`: Converts string to number.
- `num_str(n)`: Converts number to string.
- `str_repeat(s, n)`: Repeats string `s`, `n` times.
- `char_at(s, i)`: Returns the character at index `i` (1-indexed).
- `sub_str(s, start, end)`: Returns substring from `start` to `end`.
- `format(fmt, list)`: Formats string using `{0}`, `{1}` placeholders.

## 📂 `io` Package
File system and console operations.
- `read_file(path)`: Returns contents of file at `path`.
- `write_file(path, content)`: Writes `content` to file at `path`.
- `append_file(path, content)`: Appends `content` to file at `path`.
- `delete_file(path)`: Deletes file at `path`.
- `rename_file(old, new)`: Renames/moves a file.
- `file_exists(path)`, `dir_exists(path)`: Checks if file/directory exists.
- `file_size(path)`: Returns size of file in bytes.
- `make_dir(path)`: Creates a new directory.
- `list_dir(path)`: Returns a list of filenames in a directory.
- `get_cwd()`: Returns current working directory.
- `print_inline(s)`: Prints string without a newline.

## 🧪 `type` Package
Runtime type checking and conversion.
- `to_num(v)`, `to_str(v)`: Converts value to number or string.
- `is_num(v)`, `is_str(v)`, `is_bool(v)`, `is_list_val(v)`, `is_nothing(v)`: Type checks.
- `type_of(v)`: Returns a string representing the type ("number", "string", etc.).

## 🌐 `http` Package
Simple HTTP client (requires `wininet` on Windows or `curl` on Linux).
- `http_get(url)`: Performs a GET request.
- `http_post(url, body)`: Performs a POST request with plain text.
- `http_post_json(url, json)`: Performs a POST request with `application/json` header.
- `http_put(url, body)`: Performs a PUT request.
- `http_delete(url)`: Performs a DELETE request.

## ⚙️ `sys` Package
System and process utilities.
- `args()`: Returns a list of command line arguments.
- `exit(code)`: Exits the program with `code`.
- `sleep(ms)`: Pauses execution for `ms` milliseconds.
- `env_get(name)`: Returns value of environment variable.
- `run_cmd(cmd)`: Executes a system command and returns the output.

## 🔄 Higher-Order Collection Functions (v1.2.0+)
Built-in higher-order list functions accepting actions or functions.
- `map(list, fn)`: Transforms each item using `fn` and returns a new list.
- `filter(list, fn)`: Returns a new list containing items for which `fn` returns true.
- `reduce(list, fn, initial)`: Accumulates list items into a single value using `fn(acc, item)`.

