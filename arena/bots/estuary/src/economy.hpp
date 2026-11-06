#pragma once
#include "combat.hpp"
namespace estuary {
inline void shop(bc::champion& me,bool transport_required=false) {
    using I=bc::item_type;
    // Carry one terrain-crossing option. It is consumed only for a real route
    // saving or a disconnected landing, never as ordinary short-distance travel.
    if(me.gold_count()>=(transport_required?10u:40u)&&me.can_buy(bc::consumable_type::tp_crystal)) me.buy(bc::consumable_type::tp_crystal);
    // Full-emblem objectives avoid scattering gold across unfinished passives.
    const bool caster=me.champion_name()==bc::champion_type::tavi||me.champion_name()==bc::champion_type::stellar||me.champion_name()==bc::champion_type::prism||me.champion_name()==bc::champion_type::cassi;
    for(int step=0;step<32 && bc::fuel_remaining()>10000;++step) {
        auto n=me.item_count();
        I choice=caster?I::power_shard_of_focus:I::power_shard_of_strength;
        if(n[caster?I::monster_emblem_of_focus:I::monster_emblem_of_strength]==0) choice=caster?I::monster_shard_of_focus:I::monster_shard_of_strength;
        else if(n[I::jungle_emblem_of_health]==0) choice=I::jungle_shard_of_health;
        else if(n[caster?I::treasure_emblem_of_focus:I::treasure_emblem_of_strength]==0&&bc::round_number()<1100) choice=caster?I::treasure_shard_of_focus:I::treasure_shard_of_strength;
        else if(n[I::durability_emblem_of_health]==0) choice=I::durability_shard_of_health;
        else if(me.champion_name()==bc::champion_type::ryth && n[I::power_emblem_of_health]*3<n[I::power_emblem_of_strength]) choice=I::power_shard_of_health;
        if(!me.can_buy(choice)) break;
        me.buy(choice);
    }
    if(me.gold_count()>=40&&me.can_buy(bc::consumable_type::altar_lock)) me.buy(bc::consumable_type::altar_lock);
    if(me.gold_count()>=50&&me.can_buy(bc::consumable_type::tp_crystal)) me.buy(bc::consumable_type::tp_crystal);
}
inline std::optional<P> nearest_altar(Turn& t) {
    if(t.memory.altars.empty()) return std::nullopt;
    P best=t.memory.altars[0]; float score=1e9f;
    for(P p:t.memory.altars) {
        float risk=0;
        for(const auto& e:t.enemies) if(e.is_visible()&&e.is_alive()&&dist(e.position(),p)<7) risk+=8.0f;
        float v=float(dist(t.me.position(),p))+risk;
        if(v<score) {score=v;best=p;}
    }
    return best;
}
inline bool deliver(Turn& t,bool force=false) {
    unsigned gems=t.me.gem_count();
    if(!gems) {t.memory.delivery_failures=0;t.memory.delivery_deferred_until=0;return false;}
    auto altar=nearest_altar(t); if(!altar) return false;
    // A blocked altar entrance can need substantial economic preparation.
    // Preserve the cargo while pursuing other work instead of repeating the
    // same impossible delivery every turn. An adjacent legal altar still wins.
    if(t.round<t.memory.delivery_deferred_until&&dist(t.me.position(),*altar)>bc::constants::submit_gems_range) return false;
    int distance=t.memory.altar_distance.get(t.me.position());
    if(distance==bc::distance_field::unreachable) {
        bool transport=t.me.consumables()[bc::consumable_type::tp_crystal].count()>0;
        for(const auto& a:t.me.abilities()) if(a.is_dash()) transport=true;
        if(!transport||t.memory.stalled>=12) {
            // A one-way crystal can strand a carrier on an altar-free island.
            // Recall is free and preserves gems: never require gold to escape.
            if(bc::map::spawn_region(bc::team::ally).is_set(t.me.position())) return false;
            t.memory.claim(-1);t.memory.going_home=true;t.memory.bridge_active=false;
            if(t.incoming(t.me.position())>0) evade(t);
            else if(t.fresh()) t.me.recall();
            return true;
        }
    }
    // Routing is to the union of reachable altar-adjacent tiles. Geometric
    // proximity alone must never lock a carrier onto an altar behind a wall.
    bc::bitboard altar_roots;
    for(P a:t.memory.altars) {
        altar_roots |= approach_roots(t,a,bc::constants::submit_gems_range);
        if(dist(t.me.position(),a)<=bc::constants::submit_gems_range) altar=a;
    }
    unsigned threshold=distance<=4?7:distance<=12?22:36;
    if(t.me.champion_name()==bc::champion_type::marcella) threshold=std::max(10u,threshold-5);
    bool finish=bc::global_submitted_gems(bc::team::ally)+gems>=bc::constants::winning_gems;
    bool endgame=t.round+unsigned(distance)+gems/10+15>=bc::constants::max_rounds;
    if(!force&&!finish&&!endgame&&gems<threshold) return false;
    t.memory.claim(-1);
    if(dist(t.me.position(),*altar)<=bc::constants::submit_gems_range) {
        for(const auto& lock:bc::vision::altar_locks(bc::team::enemy)) if(lock.position()==*altar) {
            if(t.me.can_attack()) {t.me.attack(lock);t.memory.last_progress_round=t.round;}
            return true;
        }
        // A friendly lock denies opposing tributes; it does not prevent the
        // opponent placing its own lock or cancelling this champion's channel.
        auto& lock=t.me.consumables()[bc::consumable_type::altar_lock];
        if(lock.count()>0&&!t.me.restrictions().contains(bc::buff_restrictions::consumable)) {
            bool own=false;
            for(const auto& l:bc::vision::altar_locks(bc::team::ally)) if(l.position()==*altar) own=true;
            if(!own) lock.use(*altar);
        }
        if(t.fresh()&&t.me.is_alive()&&!t.me.restrictions().contains(bc::buff_restrictions::cast)) t.me.submit_gems(*altar);
        t.memory.delivery_failures=0;
        return true;
    }
    place_ward(t,*altar);
    if(navigate(t,std::move(altar_roots))) {
        t.memory.delivery_failures=0;
        return true;
    }
    if(++t.memory.delivery_failures>=3) {
        t.memory.delivery_failures=0;t.memory.delivery_deferred_until=t.round+40;
        t.memory.bridge_active=false;
        if(t.me.gold_count()>=30) t.memory.going_home=true;
    }
    // Failure is itself this turn's planning result. In particular, do not
    // follow a full route search with a second delivery or camp search.
    return true;
}
inline bool recover(Turn& t) {
    bool on_spawn=bc::map::spawn_region(bc::team::ally).is_set(t.me.position());
    if(on_spawn) {
        t.memory.going_home=false;
        t.memory.recovery_delivery_attempted=false;
        t.memory.home_walk_failed=false;
        // Spawn heals fully at the champion's turn end. Allow one such end
        // after arrival, then act even if an awakened camp damages the spawn
        // again before the next turn. A 99% start-of-turn test can loop forever.
        if(t.memory.spawn_rest_turns<1) {
            ++t.memory.spawn_rest_turns;
            if(t.me.health()<t.me.max_health()*0.99f) {t.memory.claim(-1);return true;}
        }
        return false;
    }
    t.memory.spawn_rest_turns=0;
    bool tavi=t.me.champion_name()==bc::champion_type::tavi;
    if(t.me.health()<t.me.max_health()*0.30f||(!tavi&&t.me.gold_count()>=330)) t.memory.going_home=true;
    if(!t.memory.going_home) return false;
    t.memory.claim(-1);
    if(!t.memory.recovery_delivery_attempted&&t.me.gem_count()>0&&t.me.health()>t.me.max_health()*0.22f&&t.memory.altar_distance.get(t.me.position())<=7) {
        t.memory.recovery_delivery_attempted=true;
        deliver(t,true);
        // Whether the route succeeded or failed, do not recursively retry a
        // delivery in this turn. A failed rescue proceeds home next turn.
        return true;
    }
    if(t.incoming(t.me.position())>0) {evade(t);return true;}
    if(!t.memory.home_walk_failed&&t.memory.home_distance.get(t.me.position())<=5) {
        if(!navigate(t,bc::map::spawn_region(bc::team::ally),true)) t.memory.home_walk_failed=true;
        // A nearby spawn can still be dynamically blocked. If the walk failed,
        // use free recall on the next fresh turn rather than search it forever.
    }
    else if(t.fresh()) t.me.recall();
    return true;
}
}
