set -e

clang -O0 -Werror -std=c11 main.c -o cache -lpthread -Wno-void-pointer-to-int-cast -Wno-int-to-void-pointer-cast -DNDEBUG=1 -DSUBLEQ=1 -s
