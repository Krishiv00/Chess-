@echo off

echo Compiling generator...
g++ -I../include -std=c++23 opening_book_generator.cpp ../src/Engine/Chess.cpp -o book
if errorlevel 1 (
    echo ERROR: Compilation failed
    del /q Chess.cpp
    exit /b 1
)

echo Generating opening book...
.\book > book.txt
if errorlevel 1 (
    echo ERROR: Book generation failed
    del /q book.exe Chess.cpp
    exit /b 1
)

echo Cleaning up...
del /q book.exe

echo Done! Opening book saved to book.txt