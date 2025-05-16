// identity
#include "orc/fixed_vector.hpp"

// stdc++
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// gtest
#include <gtest/gtest.h>

using namespace orc;

// Constructor tests
TEST(FixedVectorTest, DefaultConstructor) {
    fixed_vector<int, 5> vec;
    EXPECT_TRUE(vec.empty());
    EXPECT_EQ(vec.size(), 0);
}

TEST(FixedVectorTest, FillConstructor) {
    fixed_vector<int, 5> vec(3, 0);
    EXPECT_EQ(vec.size(), 3);
}

// Element access tests
TEST(FixedVectorTest, AtAccess) {
    fixed_vector<int, 5> vec;
    vec.push_back(0);
    EXPECT_NO_THROW(vec.at(0));
    EXPECT_THROW(vec.at(1), std::out_of_range);
}

TEST(FixedVectorTest, OperatorBracketAccess) {
    fixed_vector<int, 5> vec;
    vec.push_back(0);
    EXPECT_NO_THROW(vec[0]);
}

TEST(FixedVectorTest, FrontBackAccess) {
    fixed_vector<int, 5> vec;
    vec.push_back(0);
    vec.push_back(1);
    EXPECT_NO_THROW(vec.front());
    EXPECT_EQ(vec.front(), 0);
    EXPECT_NO_THROW(vec.back());
    EXPECT_EQ(vec.back(), 1);
}

// Iterator tests
TEST(FixedVectorTest, IteratorOperations) {
    fixed_vector<int, 5> vec;
    vec.push_back(0);
    vec.push_back(1);
    
    EXPECT_EQ(std::distance(vec.begin(), vec.end()), 2);
    EXPECT_EQ(std::distance(vec.cbegin(), vec.cend()), 2);
    EXPECT_EQ(std::distance(vec.rbegin(), vec.rend()), 2);
}

// Capacity tests
TEST(FixedVectorTest, CapacityOperations) {
    fixed_vector<int, 5> vec;
    EXPECT_EQ(vec.max_size(), 5);
    EXPECT_EQ(vec.capacity(), 5);
    EXPECT_TRUE(vec.empty());
    
    vec.push_back(0);
    EXPECT_FALSE(vec.empty());
    EXPECT_EQ(vec.size(), 1);
}

// Modifier tests
TEST(FixedVectorTest, PushBack) {
    fixed_vector<int, 5> vec;
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(vec.push_back(i));
        EXPECT_EQ(vec.back(), i);
    }
}

TEST(FixedVectorTest, PopBack) {
    fixed_vector<int, 5> vec;
    vec.push_back(0);
    EXPECT_EQ(vec.size(), 1);
    EXPECT_NO_THROW(vec.pop_back());
    EXPECT_EQ(vec.size(), 0);
}

TEST(FixedVectorTest, Clear) {
    fixed_vector<int, 5> vec;
    vec.push_back(0);
    vec.push_back(1);
    EXPECT_EQ(vec.size(), 2);
    vec.clear();
    EXPECT_TRUE(vec.empty());
}

TEST(FixedVectorTest, Insert) {
    fixed_vector<int, 5> vec;
    vec.push_back(1);
    auto it = vec.insert(vec.begin(), 0);
    EXPECT_EQ(it, vec.begin());
    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[0], 0);
    EXPECT_EQ(vec[1], 1);
}

TEST(FixedVectorTest, Erase) {
    fixed_vector<int, 5> vec;
    vec.push_back(0);
    vec.push_back(1);
    EXPECT_EQ(vec[0], 0);
    EXPECT_EQ(vec[1], 1);
    auto it = vec.erase(vec.begin());
    EXPECT_EQ(vec.size(), 1);
    EXPECT_EQ(it, vec.begin());
    EXPECT_EQ(vec[0], 1);
}

// Non-member function tests
TEST(FixedVectorTest, ComparisonOperators) {
    fixed_vector<int, 5> vec1;
    fixed_vector<int, 5> vec2;
    
    vec1.push_back(0);
    vec2.push_back(0);
    
    EXPECT_TRUE(vec1 == vec2);
    EXPECT_FALSE(vec1 != vec2);
}

TEST(FixedVectorTest, Swap) {
    fixed_vector<int, 5> vec1;
    fixed_vector<int, 5> vec2;

    vec1.push_back(0);
    vec2.push_back(0);
    vec2.push_back(1);
    
    swap(vec1, vec2);
    EXPECT_EQ(vec1.size(), 2);
    EXPECT_EQ(vec2.size(), 1);
}

// Special test for string type
TEST(FixedVectorTest, StringOperations) {
    fixed_vector<std::string, 5> vec;
    vec.push_back("hello");
    vec.push_back("world");
    
    EXPECT_EQ(vec[0], "hello");
    EXPECT_EQ(vec[1], "world");
    EXPECT_EQ(vec.size(), 2);
}

// Test for move semantics
TEST(FixedVectorTest, MoveOperations) {
    fixed_vector<std::string, 5> vec1;
    vec1.push_back("hello");
    
    fixed_vector<std::string, 5> vec2(std::move(vec1));
    EXPECT_TRUE(vec1.empty());
    EXPECT_EQ(vec2.size(), 1);
    EXPECT_EQ(vec2[0], "hello");
    
    fixed_vector<std::string, 5> vec3;
    vec3 = std::move(vec2);
    EXPECT_TRUE(vec2.empty());
    EXPECT_EQ(vec3.size(), 1);
    EXPECT_EQ(vec3[0], "hello");
}

// Test for range-based for loop
TEST(FixedVectorTest, RangeBasedFor) {
    fixed_vector<int, 5> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    
    int sum = 0;
    for (const auto& x : vec) {
        sum += x;
    }
    EXPECT_EQ(sum, 6);
}

// Test for reverse iterators
TEST(FixedVectorTest, ReverseIterators) {
    fixed_vector<int, 5> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    
    std::vector<int> reversed;
    for (auto it = vec.rbegin(); it != vec.rend(); ++it) {
        reversed.push_back(*it);
    }
    
    EXPECT_EQ(reversed.size(), 3);
    EXPECT_EQ(reversed[0], 3);
    EXPECT_EQ(reversed[1], 2);
    EXPECT_EQ(reversed[2], 1);
}
