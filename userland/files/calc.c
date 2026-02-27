#include "../userlib.h"

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
        prints("0"); 
        return; 
    }
    if (n < 0) { 
        prints("-"); 
        n = -n; 
    }
    while (n > 0) { 
        buf[i++] = '0' + (n % 10); 
        n /= 10; 
    }
    for (int j = i - 1; j >= 0; j--) {
        char s[2] = { buf[j], '\0' };
        prints(s);
    }
}

static void print_usage(void) {
    prints(COLOR_BOLD "Usage: " COLOR_RESET);
    prints("calc <num1> <operator> <num2>\n");
    prints(COLOR_CYAN "Operators: " COLOR_RESET);
    prints("+  -  *  /  %%  ^  (^ for power)\n");
    prints(COLOR_YELLOW "Example: " COLOR_RESET);
    prints("calc 5 + 3\n");
}

static long long power(int base, int exp) {
    long long result = 1;
    for (int i = 0; i < exp; i++) {
        result *= base;
    }
    return result;
}

static void print_result(long long result) {
    prints(COLOR_GREEN COLOR_BOLD "Result: " COLOR_RESET);
    
    if (result < 0) {
        prints("-");
        result = -result;
    }
    
    char buf[32];
    int i = 0;
    if (result == 0) {
        prints("0");
    } else {
        while (result > 0) {
            buf[i++] = '0' + (result % 10);
            result /= 10;
        }
        for (int j = i - 1; j >= 0; j--) {
            char s[2] = { buf[j], '\0' };
            prints(s);
        }
    }
    prints("\n");
}

static int is_operator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^';
}

int main(int argc, char *argv[]) {
    prints("\n=== Calculator ===\n");
    
    // Check argument count
    if (argc != 4) {
        prints(COLOR_RED "Error: " COLOR_RESET);
        prints("Invalid number of arguments!\n");
        prints("Expected: 3 arguments, got: ");
        print_int(argc - 1);
        prints("\n\n");
        print_usage();
        exit(1);
    }
    
    // Parse arguments
    int num1 = atoi(argv[1]);
    int num2 = atoi(argv[3]);
    char op = argv[2][0];
    
    // Validate operator
    if (argv[2][1] != '\0' || !is_operator(op)) {
        prints(COLOR_RED "Error: " COLOR_RESET);
        prints("Invalid operator: '");
        prints(argv[2]);
        prints("'\n");
        prints("Valid operators: +, -, *, /, %, ^\n");
        exit(1);
    }
    
    // Show calculation
    prints(COLOR_YELLOW "\nCalculation: " COLOR_RESET);
    print_int(num1);
    prints(" ");
    char op_str[2] = { op, '\0' };
    prints(op_str);
    prints(" ");
    print_int(num2);
    prints(" = ");
    
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
                prints(COLOR_RED "Error: Division by zero!\n" COLOR_RESET);
                error = 1;
            } else {
                result = num1 / num2;
            }
            break;
        case '%':
            if (num2 == 0) {
                prints(COLOR_RED "Error: Modulo by zero!\n" COLOR_RESET);
                error = 1;
            } else {
                result = num1 % num2;
            }
            break;
        case '^':
            if (num2 < 0) {
                prints(COLOR_RED "Error: Negative exponent not supported!\n" COLOR_RESET);
                error = 1;
            } else {
                result = power(num1, num2);
            }
            break;
        default:
            prints(COLOR_RED "Error: Unknown operator!\n" COLOR_RESET);
            error = 1;
    }
    
    if (!error) {
        print_result(result);
        
        // Show additional info for division
        if (op == '/' && num1 % num2 != 0) {
            prints(COLOR_MAGENTA "Note: " COLOR_RESET);
            prints("Integer division truncated (remainder: ");
            print_int(num1 % num2);
            prints(")\n");
        }
    }
    
    prints("\n");
    exit(0);
    return 0;
}
