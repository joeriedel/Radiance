// NodeStack.inl
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.


namespace container {

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//
// container::NodeStack
//
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////
// NodeStack::Push()
//////////////////////////////////////////////////////////////////////////////////////////

inline void NodeStack::Push(NodeList::Node* n)
{
	InsertFirst(n);
}

//////////////////////////////////////////////////////////////////////////////////////////
// NodeStack::Pop()
//////////////////////////////////////////////////////////////////////////////////////////

inline NodeStack::Node* NodeStack::Pop()
{
	return RemoveFirst();
}

//////////////////////////////////////////////////////////////////////////////////////////
// NodeStack::NodeStack()
//////////////////////////////////////////////////////////////////////////////////////////

NodeStack::NodeStack(const NodeStack& s)
{
}

//////////////////////////////////////////////////////////////////////////////////////////
// NodeStack::operator = ()
//////////////////////////////////////////////////////////////////////////////////////////

NodeStack& NodeStack::operator = (const NodeStack& s)
{
	return *this;
}

} // container

