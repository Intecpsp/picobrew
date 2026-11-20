#pragma once
#include "picoferm/data.hpp"
#include <cmath>
#include <cstring>
#include <string>
// Boundary model of the pinned Arduino 2.4.1 fixed-format digit algorithm.
// Original-instruction fixtures execute the SDK formatter itself separately.
struct FakeFormatter final:picoferm::oem::Formatter {
    void decimal(float input,char* destination,std::size_t capacity) override {
        double value=input;std::string text;
        if(std::isnan(value))text="nan";
        else if(std::isinf(value))text="inf";
        else {
            const bool negative=value<0;if(negative)value=-value;value+=0.005;
            unsigned integers=1;double scale=1;while(value>=scale*10){scale*=10;++integers;}value/=scale;
            const int padding=1-static_cast<int>(integers)-(negative?1:0);if(padding>0)text.append(padding,' ');if(negative)text+='-';
            for(unsigned i=0;i<integers+2;++i){int digit=static_cast<int>(value);if(digit>9)digit=9;text+=static_cast<char>('0'|digit);if(i+1==integers)text+='.';value=(value-digit)*10;}
        }
        if(capacity){const auto length=text.size()<capacity?text.size():capacity-1;std::memcpy(destination,text.data(),length);destination[length]=0;}
    }
};
