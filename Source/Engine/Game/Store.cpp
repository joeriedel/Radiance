/*! \file Store.cpp
	\copyright Copyright (c) 2013 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
*/

#include RADPCH
#include "Store.h"
#include "../World/World.h"

namespace iap {

///////////////////////////////////////////////////////////////////////////////

void PaymentRequest::PushElements(lua_State *L) {
	lua_pushcfunction(L, lua_Submit);
	lua_setfield(L, -2, "Submit");
	LUART_REGISTER_GET(L, Id);
	LUART_REGISTER_GET(L, Quantity);
}

int PaymentRequest::lua_Submit(lua_State *L) {
	Ref self = Get<PaymentRequest>(L, "PaymentRequest", 1, true);
	self->Submit();
	return 0;
}

LUART_GET(PaymentRequest, Id, String, m_id, Ref self = Get<PaymentRequest>(L, "PaymentRequest", 1, true))
LUART_GET(PaymentRequest, Quantity, int, m_quantity, Ref self = Get<PaymentRequest>(L, "PaymentRequest", 1, true))

///////////////////////////////////////////////////////////////////////////////

void Transaction::PushElements(lua_State *L) {
	lua_pushcfunction(L, lua_Finish);
	lua_setfield(L, -2, "Finish");
	lua_pushcfunction(L, lua_GetErrorMessage);
	lua_setfield(L, -2, "GetErrorMessage");
	LUART_REGISTER_GET(L, ProductId);
	LUART_REGISTER_GET(L, State);
}

int Transaction::lua_Finish(lua_State *L) {
	Ref self = Get<Transaction>(L, "Finish", 1, true);
	self->Finish();
	return 0;
}

int Transaction::lua_GetErrorMessage(lua_State *L) {
	Ref self = Get<Transaction>(L, "Transaction", 1, true);
	String msg = self->GetErrorMessage();
	if (!msg.empty) {
		lua::Marshal<String>::Push(L, msg);
		return 1;
	}
	return 0;
}

LUART_GET(Transaction, ProductId, String, m_productId, Ref self = Get<Transaction>(L, "Transaction", 1, true))
LUART_GET(Transaction, State, int, state.get(), Ref self = Get<Transaction>(L, "Transaction", 1, true))

///////////////////////////////////////////////////////////////////////////////

void Store::BindEventQueue(StoreEventQueue &queue) {
	queue.Bind(*this);
}

void StoreEventQueue::Bind(Store &store) {
	store.OnProductInfoResponse.Bind<StoreEventQueue>(
		this,
		&StoreEventQueue::OnProductInfoResponse,
		ManualReleaseEventTag
	);
	
	store.OnApplicationValidateResult.Bind<StoreEventQueue>(
		this,
		&StoreEventQueue::OnApplicationValidateResult,
		ManualReleaseEventTag
	);
	
	store.OnProductValidateResult.Bind<StoreEventQueue>(
		this,
		&StoreEventQueue::OnProductValidateResult,
		ManualReleaseEventTag
	);

	store.OnUpdateTransaction.Bind<StoreEventQueue>(
		this,
		&StoreEventQueue::OnUpdateTransaction,
		ManualReleaseEventTag
	);

	store.OnRestoreProductsComplete.Bind<StoreEventQueue>(
		this,
		&StoreEventQueue::OnRestoreProductsComplete,
		ManualReleaseEventTag
	);
}

void StoreEventQueue::Dispatch(world::World &target) {
	Event::Vec pending;
	{
		Lock L(m_cs);
		pending.swap(m_events);
	}

	for (Event::Vec::iterator it = pending.begin(); it != pending.end(); ++it) {
		const Event::Ref &e = *it;
		e->Dispatch(target);
	}
}

void StoreEventQueue::OnProductInfoResponse(const Product::Vec &products) {
	PostEvent(new ProductInfoResponseEvent(products));
}

void StoreEventQueue::OnApplicationValidateResult(const ResponseCode &code) {
	PostEvent(new ApplicationValidateResultEvent(code));
}

void StoreEventQueue::OnProductValidateResult(const ProductValidationData &data) {
	PostEvent(new ProductValidateResultEvent(data));
}

void StoreEventQueue::OnUpdateTransaction(const TransactionRef &transaction) {
	PostEvent(new UpdateTransactionEvent(transaction));
}

void StoreEventQueue::OnRestoreProductsComplete(const RestorePurchasesCompleteData &data) {
	PostEvent(new RestoreProductsCompleteEvent(data));
}

void StoreEventQueue::PostEvent(Event *e) {
	Lock L(m_cs);
	m_events.push_back(Event::Ref(e));
}

void StoreEventQueue::ProductInfoResponseEvent::Dispatch(world::World &target) {
	target.OnProductInfoResponse(m_products);
}

void StoreEventQueue::ApplicationValidateResultEvent::Dispatch(world::World &target) {
	target.OnApplicationValidateResult(m_code);
}

void StoreEventQueue::ProductValidateResultEvent::Dispatch(world::World &target) {
	target.OnProductValidateResult(m_data);
}

void StoreEventQueue::UpdateTransactionEvent::Dispatch(world::World &target) {
	target.OnUpdateTransaction(m_transaction);
}

void StoreEventQueue::RestoreProductsCompleteEvent::Dispatch(world::World &target) {
	target.OnRestoreProductsComplete(m_data);
}

} // iap
