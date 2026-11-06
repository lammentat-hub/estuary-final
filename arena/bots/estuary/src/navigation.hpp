#pragma once
#include "state.hpp"
namespace estuary {
inline bc::bitboard obstacles(const Turn& t,bool for_dash=false) {
    auto b=bc::map::walls()|bc::map::altars()|bc::map::spawn_region(bc::team::enemy)|bc::vision::champion_tiles()|bc::vision::camp_tiles()|bc::vision::teleport_tiles();
    if(!for_dash) b|=bc::map::holes();
    b.set(t.me.position(),false);
    return b;
}
inline bool navigate(Turn& t,bc::bitboard roots,bool escape=false,bool allow_bridge=true) {
    if(!t.budget(10000)||!t.me.can_move()) return false;
    P from=t.me.position();
    bc::bitboard original_roots=roots;
    bool following_bridge=false;
    if(t.memory.bridge_active) {
        if(t.round>t.memory.bridge_expiry||!(roots^t.memory.bridge_goal).is_empty()||t.memory.bridge_roots.is_set(from)) t.memory.bridge_active=false;
        else {roots=t.memory.bridge_roots;following_bridge=true;allow_bridge=false;}
    }
    auto blocked=obstacles(t);
    roots &= ~blocked;
    if(roots.is_empty()||roots.is_set(from)) return false;
    bc::bitboard reached, destinations;
    destinations.set(from);
    bc::distance_field field;
    if(!t.begin_search()) return false;
    field.compute_to(blocked,roots,destinations,reached);
    if(!t.budget(5000)) return false;
    // Dash only to a legal reachable landing; holes may be crossed but not landed on.
    for(const auto& a:t.me.abilities()) if(a.is_dash()&&t.budget(32000)&&t.can_cast(a)&&field.get(from)>2) {
        auto dash_blocked=obstacles(t,true);
        auto candidates=dash_blocked.reachable_tiles(from,std::uint16_t(a.get<bc::ability_dash>().range));
        candidates &= ~bc::map::holes();
        P best=from; float score=float(field.get(from));
        for(P p:candidates.positions()) if(p!=from) {
            if(!t.budget(6000)) break;
            const auto distance=field.get(p);
            if(distance==bc::distance_field::unreachable) continue;
            float v=float(distance)+t.incoming(p)*(escape?0.1f:0.01f);
            if(v<score-1.0f) {score=v;best=p;}
        }
        if(best!=from&&t.budget(4000)&&a.use(best)) {t.moved=true;return true;}
    }
    // Crystals cross arbitrary intervening terrain. Three channel ticks plus an
    // execution turn must save at least six walking steps, or bridge a component.
    const auto& crystal=t.me.consumables()[bc::consumable_type::tp_crystal];
    if(t.budget(25000)&&crystal.count()>0&&!t.me.restrictions().contains(bc::buff_restrictions::consumable)&&t.incoming(from)<t.me.health()*0.15f) {
        bc::bitboard candidates; candidates.draw_chebyshev(from,3); candidates &= ~blocked;
        P best=from; int best_distance=int(field.get(from))-6;
        for(P p:candidates.positions()) if(p!=from) {
            if(!t.budget(6000)) break;
            int d=field.get(p);if(d==bc::distance_field::unreachable)continue;
            if(d<best_distance&&t.incoming(p)<t.me.health()*0.15f) {best=p;best_distance=d;}
        }
        if(best!=from&&t.budget(4000)&&crystal.use(best)) return true;
    }
    bool dash_available=false;
    for(const auto& a:t.me.abilities()) if(a.is_dash()) dash_available=true;
    if(allow_bridge&&field.get(from)==bc::distance_field::unreachable&&(crystal.count()>0||dash_available)&&t.budget(18000)) {
        // Move to a launch shore first; otherwise a carrier far from the gap
        // would see every adjacent distance as unreachable and stand forever.
        auto near_target=reached;
        if(crystal.count()>0) {
            for(int d=1;d<=3;++d) {
                if(!t.budget(9000)) return false;
                near_target|=reached.shift_right(d);near_target|=reached.shift_right(-d);
            }
            auto horizontal=near_target;
            for(int d=1;d<=3;++d) {
                if(!t.budget(9000)) return false;
                near_target|=horizontal.shift_up(d);near_target|=horizontal.shift_up(-d);
            }
        } else {
            auto dash_blocked=obstacles(t,true);
            for(int step=0;step<2;++step) {
                auto previous=near_target;
                for(P d:directions) {
                    if(!t.budget(7000)) return false;
                    near_target |= previous.shift_right(d.x).shift_up(d.y);
                }
                near_target &= ~dash_blocked;
            }
        }
        near_target &= ~blocked;
        if(!near_target.is_empty()&&!near_target.is_set(from)) {
            // Finish this planning turn here. A second full distance field in
            // the same 100k-fuel turn can suspend execution into the next round.
            // The next turn uses fresh observations to route to this shoreline.
            t.memory.bridge_active=true;t.memory.bridge_expiry=t.round+120;
            t.memory.bridge_goal=std::move(original_roots);
            t.memory.bridge_roots=std::move(near_target);
            // A persisted plan has handled this turn's objective even though
            // no move occurred. The caller must not replace it with a recall.
            return true;
        }
    }
    // A hidden occupant returns false without consuming movement. Recompute the mask
    // before trying alternatives; never deliberately step onto a known blocker.
    for(int attempt=0;attempt<3&&t.me.can_move()&&t.budget(18000);++attempt) {
        blocked=obstacles(t);
        auto candidates=blocked.reachable_tiles(from,1).positions();
        P best=from; float best_score=1e9f;
        for(P p:candidates) if(p!=from) {
            if(!t.budget(4000)) break;
            auto d=field.get(p); if(d==bc::distance_field::unreachable) continue;
            float score=float(d)+t.incoming(p)*(escape?0.08f:0.008f);
            if(p==t.memory.previous) score+=0.25f;
            if(score<best_score) {best_score=score;best=p;}
        }
        if(best==from) {if(following_bridge)t.memory.bridge_active=false;return false;}
        if(t.budget(2500)&&t.me.move_absolute(best)) {t.moved=true;return true;}
        // Keep this field for alternate adjacent choices. Dynamic occupancy is
        // refreshed above; a full new route is deferred until the next turn.
    }
    return false;
}
inline bc::bitboard approach_roots(const Turn& t,P target,int range) {
    bc::bitboard result;
    result.draw_chebyshev(target,range);
    result &= ~t.memory.terrain;
    return result;
}
inline bool evade(Turn& t) {
    if(!t.budget(25000)||!t.me.can_move()) return false;
    auto blocked=obstacles(t);
    P from=t.me.position(),best=from;
    auto safety=[&](P p) {
        float v=t.incoming(p)*10.0f+float(t.memory.home_distance.get(p))*0.1f;
        for(const auto& threat:t.threats) v+=(threat.camp?10.0f:30.0f)/(1+dist(p,threat.position));
        return v;
    };
    float score=safety(from);
    for(P p:blocked.reachable_tiles(from,1).positions()) {
        float v=safety(p);
        if(v<score) {score=v;best=p;}
    }
    if(best!=from&&t.me.move_absolute(best)) {t.moved=true;return true;}
    return navigate(t,bc::map::spawn_region(bc::team::ally),true);
}
inline void place_ward(Turn& t,P toward) {
    if(!t.budget(28000)||t.me.ward_cooldown()>0||t.me.restrictions().contains(bc::buff_restrictions::ward)) return;
    if(int(t.round)-t.memory.last_ward<10) return;
    auto b=obstacles(t)|bc::vision::ward_tiles(bc::team::ally); P from=t.me.position(),best=from; int score=dist(from,toward);
    for(P d:directions) { P p=from+d; if(bc::map::is_in_bounds(p)&&!b.is_set(p)&&dist(p,toward)<score) {best=p;score=dist(p,toward);} }
    if(best!=from&&t.me.place_ward(best)) {t.memory.last_ward=int(t.round);t.visible=bc::vision::visible_tiles();}
}
}
