// IntegralConstant.h
// Copyright (c) 2005-2010 Pyramind Labs LLC, All Rights Reserved
// Author: Mike Songy
// See Radiance/LICENSE for licensing terms.

#pragma once

#include "IntMeta.h"


namespace meta {

//////////////////////////////////////////////////////////////////////////////////////////
// meta::IntegralConstant<T, Value>
//////////////////////////////////////////////////////////////////////////////////////////

template <typename T, T Value>
struct IntegralConstant
{
	typedef IntegralConstant<T, Value> SelfType;
	typedef T                          ValueType;
	static const T VALUE = Value;
};

//////////////////////////////////////////////////////////////////////////////////////////
// meta::BoolConstant<bool>
//////////////////////////////////////////////////////////////////////////////////////////

template <bool Value>
struct BoolConstant :
public IntegralConstant<bool, Value>
{
	typedef IntegralConstant<bool, Value> SuperType;
	typedef BoolConstant<Value>           SelfType;
};

//////////////////////////////////////////////////////////////////////////////////////////
// meta::TrueType
//////////////////////////////////////////////////////////////////////////////////////////

struct TrueType :
public BoolConstant<true>
{
	int __PAD__;
};

//////////////////////////////////////////////////////////////////////////////////////////
// meta::FalseType
//////////////////////////////////////////////////////////////////////////////////////////

struct FalseType :
public BoolConstant<false>
{
};

//////////////////////////////////////////////////////////////////////////////////////////
// meta::IntConstant<int>
//////////////////////////////////////////////////////////////////////////////////////////

template <int Value>
struct IntConstant :
public IntegralConstant<int, Value>
{
	typedef IntegralConstant<int, Value> SuperType;
	typedef IntConstant<Value>           SelfType;
};

} // meta
