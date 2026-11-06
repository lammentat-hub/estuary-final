#pragma once
#include "cambc.h"
#include <cmath>
#include <cstdio>

// Estuary: an original, deterministic resource-routing bot, 23 October 2026.
namespace estuary {
namespace bc = cambc;
using P = bc::vec2;
inline int dist(P a, P b) { return int(a.chebyshev_distance(b)); }
inline unsigned id(const bc::champion& c) { return unsigned(static_cast<bc::handle_champion_t>(c)); }
inline unsigned id(const bc::map::camp_group& g) { return unsigned(static_cast<bc::handle_camp_group_t>(g)); }
inline constexpr P directions[]={{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
struct CampRecord {
    bc::map::camp_group group;
    P centre;
    float hp=0, attack=0;
    int next_ready=0, avoid_until=0;
    bool reward=false;
    CampRecord(bc::map::camp_group g):group(std::move(g)),centre(group.camps()[0].spawn_position()) {
        for(const auto& c:group) { auto s=c.base_stats(); hp+=s.base_health; attack+=s.base_strength; }
    }
};
struct Memory {
    bool initialized=false, going_home=false, transport_required=false;
    bool bridge_active=false;
    bool no_task_pending=false, recovery_delivery_attempted=false, home_walk_failed=false;
    bool assisting=false;
    unsigned spawn_rest_turns=0;
    unsigned delivery_deferred_until=0, delivery_failures=0;
    unsigned last_progress_round=0, last_gold=0, last_gems=0;
    unsigned bridge_expiry=0;
    unsigned round=0, last_round=0;
    int slot=0, goal=-1, goal_since=0, stalled=0, last_ward=-100;
    P previous{-1,-1};
    std::vector<CampRecord> groups;
    std::vector<bc::champion> allies;
    std::vector<P> altars;
    bc::bitboard terrain;
    bc::bitboard bridge_goal, bridge_roots;
    bc::distance_field home_distance, altar_distance;
    void initialize() {
        terrain=bc::map::walls()|bc::map::holes()|bc::map::altars()|bc::map::spawn_region(bc::team::enemy);
        allies=bc::champions(bc::team::ally);
        std::sort(allies.begin(),allies.end(),[](auto& a,auto& b){return id(a)<id(b);});
        for(int i=0;i<int(allies.size());++i) if(id(allies[i])==id(bc::self())) slot=i;
        for(auto& g:bc::map::camp_groups()) groups.emplace_back(std::move(g));
        altars=bc::map::altars().positions();
        bc::bitboard reached;
        home_distance.compute(terrain,bc::map::spawn_region(bc::team::ally),reached);
        bc::bitboard altar_roots;
        for(P a:altars) for(P d:directions) { P p=a+d; if(bc::map::is_in_bounds(p)&&!terrain.is_set(p)) altar_roots.set(p); }
        altar_distance.compute(terrain,altar_roots,reached);
        auto spawns=bc::map::spawn_region(bc::team::ally).positions();
        transport_required=!spawns.empty()&&altar_distance.get(spawns[0])==bc::distance_field::unreachable;
        initialized=true;
    }
    void claim(int g) {
        goal=g;
        if(g<0) assisting=false;
        if(slot<int(bc::shared_memory.size())) bc::shared_memory[slot]=g>=0&&g<255?std::uint8_t(g+1):0;
    }
    std::array<std::uint8_t,8> claims() const {
        std::array<std::uint8_t,8> result{};
        auto n=std::min(std::size_t(8),bc::shared_memory.size());
        bc::shared_memory.read(0,std::span<std::uint8_t>{result.data(),n});
        for(int i=0;i<int(allies.size())&&i<8;++i) if(!allies[i].is_alive()) result[i]=0;
        return result;
    }
};
struct Turn {
    struct Threat { P position; int range; float damage; bool camp; };
    Memory& memory;
    bc::champion& me=bc::self();
    unsigned round=bc::round_number();
    bool moved=false;
    unsigned searches=0;
    bc::bitboard visible=bc::vision::visible_tiles();
    std::vector<bc::camp> camps=bc::vision::camps();
    std::vector<bc::champion> enemies=bc::vision::champions(bc::team::enemy);
    std::vector<bc::champion> friends=bc::vision::champions(bc::team::ally);
    std::vector<Threat> threats;
    explicit Turn(Memory& m):memory(m){
        // Navigation can score dozens of candidate tiles. Capture these small
        // records once instead of making thousands of repeated host queries.
        // A camp killed later in this turn remains a conservative risk estimate.
        for(const auto& e:enemies) if(e.is_visible()&&e.is_alive()) threats.push_back({e.position(),e.attack_range()+1,e.strength()+e.focus()*0.22f,false});
        for(const auto& c:camps) if(c.is_visible()&&c.is_alive()&&c.is_awake()&&c.strength()>0) threats.push_back({c.position(),c.attack_range(),c.strength(),true});
    }
    bool fresh() const { return bc::round_number()==round; }
    bool budget(unsigned n=9000) const {return fresh()&&bc::fuel_remaining()>n;}
    bool begin_search() {if(searches||!budget(50000))return false;++searches;return true;}
    float incoming(P p,bool include_camps=true) const {
        float value=0;
        for(const auto& threat:threats) if((include_camps||!threat.camp)&&dist(p,threat.position)<=threat.range) value+=threat.damage;
        return value;
    }
    bool can_cast(const bc::ability& a) const {
        if(!fresh()||!a.is_ready()) return false;
        return !moved || !std::visit([](auto& v){return v.exclusive_with_movement;},*a.info());
    }
};
}
