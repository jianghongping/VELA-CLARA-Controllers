/*
//              This file is part of VELA-CLARA-Controllers.                          //
//------------------------------------------------------------------------------------//
//    VELA-CLARA-Controllers is free software: you can redistribute it and/or modify  //
//    it under the terms of the GNU General Public License as published by            //
//    the Free Software Foundation, either version 3 of the License, or               //
//    (at your option) any later version.                                             //
//    VELA-CLARA-Controllers is distributed in the hope that it will be useful,       //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of                  //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                   //
//    GNU General Public License for more details.                                    //
//                                                                                    //
//    You should have received a copy of the GNU General Public License               //
//    along with VELA-CLARA-Controllers.  If not, see <http://www.gnu.org/licenses/>. //
//
//  Author:      DJS
//  Last edit:   19-03-2018
//  FileName:    baseObject.cpp
//  Description:
//
//
//
//*/
// project includes
#include "baseObject.h"
#include "configDefinitions.h"
// stl includes
#include <time.h>
//  __  ___  __   __    /  __  ___  __   __
// /  `  |  /  \ |__)  /  |  \  |  /  \ |__)
// \__,  |  \__/ |  \ /   |__/  |  \__/ |  \
//
//______________________________________________________________________________
size_t baseObject::basecount = UTL::ZERO_SIZET;
//______________________________________________________________________________
baseObject::baseObject(const bool& show_messages,
                       const bool& show_debug_messages,
                       const std::string& message):
SHOW_MESSAGES(show_messages),
SHOW_DEBUG_MESSAGES(show_debug_messages)
{
    basecount += UTL::ONE_SIZET;
    //debugMessage(message, " baseObject constructor, count = ", basecount);
}
//______________________________________________________________________________
baseObject::baseObject(const bool& show_messages,
                       const bool& show_debug_messages):
SHOW_MESSAGES(show_messages),
SHOW_DEBUG_MESSAGES(show_debug_messages)
{
    basecount += UTL::ONE_SIZET;
    //debugMessage("baseObject constructor, count = ", basecount);
}
//______________________________________________________________________________
baseObject::~baseObject()
{}
//______________________________________________________________________________
bool baseObject::polaritiesMatch(const std::vector<double>& vals) const
{
    bool ret = true;
    std::vector<bool> signs;
    for(auto&& it: vals)
        if(it != UTL::ZERO_DOUBLE) /// zero can have either polarity
            signs.push_back(std::signbit(it));
    for(auto&& it: signs)
        if(signs[UTL::ZERO_SIZET] != it)
            ret = false;
    return ret;
}
//______________________________________________________________________________
std::string baseObject::currentDateTime() const
{
    time_t     now = timeNow();
    struct tm  tstruct;
    char       buf[UTL::BUFFER_EIGHTY];
    localtime_s(&tstruct, &now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d-%H%M",&tstruct);
    return buf;
}
//______________________________________________________________________________
int baseObject::getNum(const std::string& str) const
{
    return atoi(str.c_str());
}
//______________________________________________________________________________
size_t baseObject::getSize(const std::string& str) const
{
    return (size_t)getNum(str);
}
//______________________________________________________________________________
double baseObject::getNumD(const  std::string& str) const
{
    return atof(str.c_str());
}
//______________________________________________________________________________
long baseObject::getNumL(const  std::string& str) const
{
    return (long)atof(str.c_str());
}
//______________________________________________________________________________
unsigned short baseObject::getNumUS(const  std::string& str) const
{
    return (unsigned short)atof(str.c_str());
}
//______________________________________________________________________________
bool baseObject::stringIsSubString(const std::string& stringToCheck,
                                   const std::string& stringToLookFor) const
{
    return stringToCheck.find(stringToLookFor) != std::string::npos;
}
//______________________________________________________________________________
bool baseObject::isDummyName(const std::string& name )const
{
    return name == UTL::DUMMY_NAME;
}
//______________________________________________________________________________
bool baseObject::isNotDummyName(const std::string& name )const
{
    return !isDummyName(name);
}






////______________________________________________________________________________
//boost::python::numpy::ndarray baseObject::toNumPyArray(const std::vector<double> & v ) const
//{
//    namespace p = boost::python;
//    namespace np = boost::python::numpy;
//    p::object own;
//    p::tuple shape = p::make_tuple(v.size());
//    p::tuple stride = p::make_tuple(sizeof(double));
//    np::dtype dt = np::dtype::get_builtin<double>();
//    return np::from_data(&v[0],dt, shape,stride,own);
//
////    Py_intptr_t shape[1] = { v.size() };
////    boost::python::numpy::ndarray result = boost::python::numpy::zeros(1, shape, boost::python::numpy::dtype::get_builtin<double>());
////    std::copy(v.begin(), v.end(), reinterpret_cast<double*>(result.get_data()));
////    return result;
//}
////______________________________________________________________________________
//boost::python::numpy::ndarray baseObject::toNumPyArray(const std::vector<double> & v )const
//{
//    namespace p = boost::python;
//    namespace np = boost::python::numpy;
//    p::object own;
//    p::tuple shape = p::make_tuple(v.size());
//    p::tuple stride = p::make_tuple(sizeof(double));
//    np::dtype dt = np::dtype::get_builtin<double>();
//    return np::from_data(&v[0],dt, shape,stride,own);
////    Py_intptr_t shape[1] = { v.size() };
////    boost::python::numpy::ndarray result = boost::python::numpy::zeros(1, shape, boost::python::numpy::dtype::get_builtin<double>());
////    std::copy(v.begin(), v.end(), reinterpret_cast<double*>(result.get_data()));
////    return result;
//}
////______________________________________________________________________________
//boost::python::numpy::ndarray baseObject::toNumPyArray(const std::vector<int>& v) const
//{
//    namespace p = boost::python;
//    namespace np = boost::python::numpy;
//    p::object own;
//    p::tuple shape = p::make_tuple(v.size());
//    p::tuple stride = p::make_tuple(sizeof(int));
//    np::dtype dt = np::dtype::get_builtin<int>();
//    return np::from_data(&v[0],dt, shape,stride,own);
//}
////______________________________________________________________________________
//boost::python::numpy::ndarray baseObject::toNumPyArray(const std::vector<int>& v) const
//{
//    namespace p = boost::python;
//    namespace np = boost::python::numpy;
//    p::object own;
//    p::tuple shape = p::make_tuple(v.size());
//    p::tuple stride = p::make_tuple(sizeof(int));
//    np::dtype dt = np::dtype::get_builtin<int>();
//    return np::from_data(&v[0],dt, shape,stride,own);
//}
////______________________________________________________________________________
//boost::python::numpy::ndarray baseObject::toNumPyArray(const std::vector<std::vector<double>>& v) const
//{
//    namespace p = boost::python;
//    namespace np = boost::python::numpy;
//    /*
//        trying to follow this
//        https://www.boost.org/doc/libs/1_63_0/libs/python/doc/html/numpy/tutorial/ndarray.html
//    */
//    p::object own;
//    /*
//        REGULAR 2D ARRAY MUST BE PASSED IN
//    */
//    p::tuple shape = p::make_tuple( v[0].size(), v.size());
//
//    p::tuple stride = p::make_tuple(sizeof(double) * v[0].size(), sizeof(double));
//
//    np::dtype dt1 = np::dtype::get_builtin<double>();
//
//    const double* a = &v[0][0];
//
//    np::ndarray r  = np::from_data(a, dt1,shape,stride, p::object());
//    return r;
//}


