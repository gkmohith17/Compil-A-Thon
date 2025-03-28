
#pragma once
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <execution>
#include <iostream>

template<typename T>
class Matrix {
private:
    std::vector<T> data;
    size_t rows;
    size_t cols;

public:
    // Constructors
    Matrix() : rows(0), cols(0) {}
    
    Matrix(size_t r, size_t c) : rows(r), cols(c), data(r * c, T{}) {}
    
    Matrix(size_t r, size_t c, const T& initial_value) 
        : rows(r), cols(c), data(r * c, initial_value) {}

    // Access operators
    T& operator()(size_t row, size_t col) {
        if (row >= rows || col >= cols)
            throw std::out_of_range("Matrix index out of bounds");
        return data[row * cols + col];
    }

    const T& operator()(size_t row, size_t col) const {
        if (row >= rows || col >= cols)
            throw std::out_of_range("Matrix index out of bounds");
        return data[row * cols + col];
    }

    // Matrix Multiplication
    Matrix<T> multiply(const Matrix<T>& other) const {
        if (cols != other.rows)
            throw std::invalid_argument("Matrix dimensions incompatible for multiplication");

        Matrix<T> result(rows, other.cols);

        // Parallel multiplication using std::execution::par
        std::for_each(std::execution::par, 
            std::begin(result.data), 
            std::end(result.data),
            [&](T& element) {
                size_t r = (&element - result.data.data()) / result.cols;
                size_t c = (&element - result.data.data()) % result.cols;

                T sum = T{};
                for (size_t k = 0; k < cols; ++k) {
                    sum += (*this)(r, k) * other(k, c);
                }
                element = sum;
            }
        );

        return result;
    }

    // Utility Methods
    void print() const {
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                std::cout << (*this)(r, c) << " ";
            }
            std::cout << std::endl;
        }
    }

    // Getters
    size_t getRows() const { return rows; }
    size_t getCols() const { return cols; }
};