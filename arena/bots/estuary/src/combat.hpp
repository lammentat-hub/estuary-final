#pragma once
#include "navigation.hpp"
namespace estuary {
template<class Target> inline bool valid_target(const Target& target) {
    return target.is_visible()&&target.health()>0;
}
template<class Target> inline bool fight(Turn& t,const Target& target) {
    if(!t.budget(12000)||!valid_target(target)) return false;
    constexpr bool enemy=std::is_same_v<Target,bc::champion>;
    const P p=target.position();
    const int range=t.me.attack_range();
    bool acted=false;
    // A shield prevents a losing exchange; otherwise reserve the shared cast for
    // damage or an attack reset. Ability parameters come from runtime metadata.
    for(const auto& a:t.me.abilities()) if(a.is_shield()&&t.can_cast(a)&&a.can_target_self()) {
        float threat=t.incoming(t.me.position());
        if(threat>0 && (t.me.health()<threat*5 || !enemy)) {
            if(!t.me.buffs().has_shield()) {acted=a.use();break;}
        }
    }
    // Select the most useful ready damage/control cast. Self AoE is evaluated
    // against its radius; ground AoE against its cast range.
    int best_slot=-1; float best_value=0;
    auto abilities=t.me.abilities();
    for(int i=0;i<2;++i) {
        auto& a=abilities[i];
        if(!t.can_cast(a)||a.is_attack_reset()||a.is_dash()||a.is_heal()||a.is_shield()) continue;
        bool allowed=enemy?a.can_target_enemies():a.can_target_camps();
        if(!allowed&&!a.can_target_ground()&&!a.can_target_self()) continue;
        float cast_range=std::visit([](auto& v)->float {if constexpr(requires{v.range;})return v.range;else return 0;},*a.info());
        if(a.can_target_self()&&a.is_aoe_damage()) cast_range=a.get<bc::ability_aoe_damage>().radius;
        if(dist(t.me.position(),p)>cast_range) continue;
        float value=std::min(target.health(),a.damage_amount());
        if(a.is_cc()) value+=(enemy?100.0f:target.strength());
        if(a.is_aoe_damage()) for(const auto& c:t.camps) if(c.is_visible()&&c.is_alive()&&dist(p,c.position())<=a.get<bc::ability_aoe_damage>().radius) value+=a.damage_amount()*0.25f;
        if(value>best_value) {best_value=value;best_slot=i;}
    }
    if(best_slot>=0) {
        auto& a=abilities[best_slot];
        bool used=false;
        if(a.can_target_self()) used=a.use();
        else if(a.can_target_ground()) used=a.use(p);
        else used=a.use(target);
        if(used) t.memory.last_progress_round=t.round;
        acted=used||acted;
    }
    if(valid_target(target)&&t.me.can_attack()&&dist(t.me.position(),target.position())<=range) {
        t.me.attack(target); acted=true;t.memory.last_progress_round=t.round;
        for(const auto& a:t.me.abilities()) if(a.is_attack_reset()&&t.can_cast(a)&&valid_target(target)) {
            if(a.use()&&t.me.can_attack()&&valid_target(target)) t.me.attack(target);
            break;
        }
    }
    return acted;
}
inline void support(Turn& t) {
    for(const auto& a:t.me.abilities()) if(a.is_heal()&&t.can_cast(a)) {
        float best=0; const bc::champion* target=nullptr;
        for(const auto& ally:t.friends) if(id(ally)!=id(t.me)&&ally.is_visible()&&ally.is_alive()&&dist(t.me.position(),ally.position())<=a.get<bc::ability_heal>().range) {
            float value=ally.max_health()-ally.health();
            if(value>best) {best=value;target=&ally;}
        }
        if(target&&best>a.heal_amount()*0.55f) {if(a.use(*target)) t.memory.last_progress_round=t.round;return;}
    }
}
inline bool enemy_opportunity(Turn& t) {
    const bc::champion* best=nullptr; float score=0;
    for(const auto& e:t.enemies) {
        if(!e.is_visible()||!e.is_alive()) continue;
        int d=dist(t.me.position(),e.position());
        if(d>std::max(4,t.me.attack_range())) continue;
        float value=(e.is_submitting_gems()?400.0f:0.0f)+float(e.gem_count())*3.0f+200.0f/(1+e.health()/t.me.strength());
        bool kill=e.health()<t.me.strength()*2+t.me.focus()*1.5f;
        bool defend=d<=e.attack_range()&&t.me.health()>e.health()*0.8f&&t.me.health()>t.incoming(t.me.position())*5;
        if((kill||defend||e.is_submitting_gems())&&value>score) {best=&e;score=value;}
    }
    return best&&fight(t,*best);
}
}
