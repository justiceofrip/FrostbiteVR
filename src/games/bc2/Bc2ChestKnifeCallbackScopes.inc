// Win32/x86 only. Keep C++ objects with destructors out of the SEH leaves.
template<class Body>
__declspec(noinline) void CountChestKnifeCallback(std::atomic<unsigned>& active,const Body& body) {
    active.fetch_add(1,std::memory_order_acq_rel);
    __try {body();}
    __finally {active.fetch_sub(1,std::memory_order_acq_rel);}
}

template<class Scope,class Original,class OnUnwind>
__declspec(noinline) std::uintptr_t ForwardChestKnifeOriginal(
    Scope** slot,Scope* current,const Original& original,const OnUnwind& onUnwind) {
    Scope* previous=slot?*slot:nullptr;
    std::uintptr_t result=0;
    __try {
        if(slot)*slot=current;
        result=original();
    }
    __finally {
        if(slot)*slot=previous;
        if(AbnormalTermination())onUnwind();
    }
    return result;
}
