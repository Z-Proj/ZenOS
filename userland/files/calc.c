#include "../userlib.h"
#include "../libs/lib.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

static void print_int(int n) {
    char buf[32];
    int i = 0;
    if (n == 0) { 
        fputs("0", stdout); 
        return; 
    }
    if (n < 0) { 
        fputs("-", stdout); 
        n = -n; 
    }
    while (n > 0) { 
        buf[i++] = '0' + (n % 10); 
        n /= 10; 
    }
    for (int j = i - 1; j >= 0; j--) {
        char s[2] = { buf[j], '\0' };
        fputs(s, stdout);
    }
}

static void print_usage(void) {
    fputs(COLOR_BOLD "Usage: " COLOR_RESET, stdout);
    fputs("calc <num1> <operator> <num2>\n", stdout);
    fputs(COLOR_CYAN "Operators: " COLOR_RESET, stdout);
    fputs("+  -  *  /  %%  ^  (^ for power)\n", stdout);
    fputs(COLOR_YELLOW "Example: " COLOR_RESET, stdout);
    fputs("calc 5 + 3\n", stdout);
}

static long long power(int base, int exp) {
    long long result = 1;
    for (int i = 0; i < exp; i++) {
        result *= base;
    }
    return result;
}

static void print_result(long long result) {
    fputs(COLOR_GREEN COLOR_BOLD "Result: " COLOR_RESET, stdout);
    
    if (result < 0) {
        fputs("-", stdout);
        result = -result;
    }
    
    char buf[32];
    int i = 0;
    if (result == 0) {
        fputs("0", stdout);
    } else {
        while (result > 0) {
            buf[i++] = '0' + (result % 10);
            result /= 10;
        }
        for (int j = i - 1; j >= 0; j--) {
            char s[2] = { buf[j], '\0' };
            fputs(s, stdout);
        }
    }
    fputs("\n", stdout);
}

static int is_operator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^';
}

int main(int argc, char *argv[]) {
    fputs("\n=== Calculator ===\n", stdout);
    
    // Check argument count
    if (argc != 4) {
        fputs(COLOR_RED "Error: " COLOR_RESET, stdout);
        fputs("Invalid number of arguments!\n", stdout);
        fputs("Expected: 3 arguments, got: ", stdout);
        print_int(argc - 1);
        fputs("\n\n", stdout);
        print_usage();
        exit(1);
    }
    
    // Parse arguments
    int num1 = atoi(argv[1]);
    int num2 = atoi(argv[3]);
    char op = argv[2][0];
    
    // Validate operator
    if (argv[2][1] != '\0' || !is_operator(op)) {
        fputs(COLOR_RED "Error: " COLOR_RESET, stdout);
        fputs("Invalid operator: '", stdout);
        fputs(argv[2], stdout);
        fputs("'\n", stdout);
        fputs("Valid operators: +, -, *, /, %, ^\n", stdout);
        exit(1);
    }
    
    // Show calculation
    fputs(COLOR_YELLOW "\nCalculation: " COLOR_RESET, stdout);
    print_int(num1);
    fputs(" ", stdout);
    char op_str[2] = { op, '\0' };
    fputs(op_str, stdout);
    fputs(" ", stdout);
    print_int(num2);
    fputs(" = ", stdout);
    
    // Perform calculation
    long long result;
    int error = 0;
    
    switch (op) {
        case '+':
            result = (long long)num1 + num2;
            break;
        case '-':
            result = (long long)num1 - num2;
            break;
        case '*':
            result = (long long)num1 * num2;
            break;
        case '/':
            if (num2 == 0) {
                fputs(COLOR_RED "Error: Division by zero!\n" COLOR_RESET, stdout);
                error = 1;
            } else {
                result = num1 / num2;
            }
            break;
        case '%':
            if (num2 == 0) {
                fputs(COLOR_RED "Error: Modulo by zero!\n" COLOR_RESET, stdout);
                error = 1;
            } else {
                result = num1 % num2;
            }
            break;
        case '^':
            if (num2 < 0) {
                fputs(COLOR_RED "Error: Negative exponent not supported!\n" COLOR_RESET, stdout);
                error = 1;
            } else {
                result = power(num1, num2);
            }
            break;
        default:
            fputs(COLOR_RED "Error: Unknown operator!\n" COLOR_RESET, stdout);
            error = 1;
    }
    
    if (!error) {
        print_result(result);
        
        // Show additional info for division
        if (op == '/' && num1 % num2 != 0) {
            fputs(COLOR_MAGENTA "Note: " COLOR_RESET, stdout);
            fputs("Integer division truncated (remainder: ", stdout);
            print_int(num1 % num2);
            fputs(")\n", stdout);
        }
    }
    
    fputs("\n", stdout);
    exit(0);
    return 0;
}
