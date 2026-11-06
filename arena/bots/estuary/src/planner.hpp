#pragma once
#include "economy.hpp"
namespace estuary {
inline bool observe_group(Turn& t,CampRecord& r) {
    if(!t.budget(7000)) return false;
    bool seen=false;
    // Effective vision can change when a ward is replaced; a turn-start mask
    // must never authorize a visibility-sensitive live SDK call.
    for(const auto& c:r.group) {
        if(!t.budget(4000)) return false;
        if(bc::vision::in_vision(c.spawn_position())) {seen=true;break;}
    }
    if(!seen) return false;
    auto state=r.group.state();
    r.reward=std::holds_alternative<bc::map::camp_group_reward_available>(state);
    if(auto* cooldown=std::get_if<bc::map::camp_group_respawning>(&state)) r.next_ready=int(t.round)+cooldown->timer+1;
    else r.next_ready=0;
    return true;
}
inline float camp_dps(Turn& t,const CampRecord& r) {
    float result=t.me.strength()+t.me.focus()*0.18f;
    auto kind=t.me.champion_name();
    if(kind==bc::champion_type::varg) result*=1.0f+0.6f*float(std::min(std::size_t(3),r.group.camps().size())-1);
    if(kind==bc::champion_type::vendetta||kind==bc::champion_type::marcella||kind==bc::champion_type::ryth) result*=1.1f;
    return std::max(10.0f,result);
}
inline int choose_camp(Turn& t) {
    auto claims=t.memory.claims();
    int best=-1,fallback=-1; float best_score=-1,fallback_score=-1;
    P pos=t.me.position();
    auto n=t.me.item_count();
    bool transport=t.me.consumables()[bc::consumable_type::tp_crystal].count()>0;
    for(const auto& a:t.me.abilities()) if(a.is_dash()) transport=true;
    bc::bitboard origin,reached;origin.set(pos);
    bc::distance_field access;
    if(!t.begin_search()) return -1;
    access.compute(t.memory.terrain,origin,reached);
    for(int i=0;i<int(t.memory.groups.size())&&t.budget(22000);++i) {
        auto& r=t.memory.groups[i];
        if(r.avoid_until>int(t.round)) continue;
        observe_group(t,r);
        int distance=dist(pos,r.centre);
        auto walk_distance=access.get(r.centre);
        if(walk_distance==bc::distance_field::unreachable) {if(!transport)continue;distance+=12;}
        else distance=walk_distance;
        if(r.next_ready>int(t.round)+distance+2) continue;
        bool assigned=false;
        for(int a=0;a<int(t.memory.allies.size())&&a<8;++a) if(a!=t.memory.slot&&i<255&&claims[a]==i+1) assigned=true;
        float hp=r.hp,damage=r.attack;
        bool all_seen=true; float live_hp=0,live_damage=0;
        for(const auto& c:r.group) {
            if(!t.budget(14000)) {all_seen=false;break;}
            if(!t.visible.is_set(c.spawn_position())) {all_seen=false;break;}
            if(c.is_visible()&&c.is_alive()) {live_hp+=c.health();live_damage+=c.strength();}
        }
        if(all_seen) {hp=live_hp;damage=live_damage;}
        float clear=r.reward?0:hp/camp_dps(t,r);
        float mitigation=n[bc::item_type::jungle_emblem_of_health]?0.7f:1.0f;
        if(n[bc::item_type::durability_emblem_of_health]) mitigation*=0.9f;
        float cost=damage*clear*0.65f*mitigation;
        float risk=0;
        for(const auto& e:t.enemies) if(e.is_visible()&&e.is_alive()&&dist(e.position(),r.centre)<7) risk+=14;
        float reward=float(r.group.gem_value())*4.0f+float(r.group.gold_value())*(t.round<400?0.7f:0.25f);
        int altar_distance=t.memory.altar_distance.get(r.centre);
        if(altar_distance==bc::distance_field::unreachable) altar_distance=30;
        float score=reward/(8.0f+distance+clear+float(altar_distance)*0.2f+risk);
        if(i==t.memory.goal) score*=1.15f;
        // Stable deterministic lane preference breaks perfectly symmetric choices.
        score*=1.0f+float((i+t.memory.slot*7)%11)*0.006f;
        if(!assigned&&(r.reward||cost<=t.me.health()*0.72f)&&score>best_score) {best_score=score;best=i;}
        // Rejecting every camp until one is solo-clearable can prevent an
        // entire team ever earning its first gold. With no safe independent
        // task, make a staged attempt against a reachable live group. Damage
        // persists while it sleeps, so retreat/heal trips retain useful work.
        // Prefer an existing attack party when costs are otherwise comparable.
        if(!r.reward&&walk_distance!=bc::distance_field::unreachable) {
            float v=(assigned?1.25f:1.0f)/(8.0f+distance+clear+cost/std::max(1.0f,t.me.max_health()));
            if(v>fallback_score) {fallback_score=v;fallback=i;}
        }
    }
    t.memory.assisting=best<0&&fallback>=0;
    return best>=0?best:fallback;
}
inline void work_camp(Turn& t) {
    if(!t.budget(35000)) return;
    int g=t.memory.goal;
    bool choose=g<0||g>=int(t.memory.groups.size());
    if(!choose) {
        auto& r=t.memory.groups[g];
        observe_group(t,r);
        if(r.next_ready>int(t.round)+2||r.avoid_until>int(t.round)) choose=true;
        auto claims=t.memory.claims();
        bool lower_claim=false;
        for(int a=0;a<t.memory.slot&&a<8;++a) if(g<255&&claims[a]==g+1) lower_claim=true;
        if(lower_claim&&r.reward) {
            auto footprint=r.group.spawn_tiles();
            if(footprint.is_set(t.me.position())) {
                // Helpers clear monsters together, then leave the reward to
                // the lowest-slot claimant. Extra bodies do not speed capture.
                auto outside=approach_roots(t,t.me.position(),3);outside &= ~footprint;
                t.memory.claim(-1);navigate(t,std::move(outside));return;
            }
            choose=true;
        } else if(lower_claim&&!t.memory.assisting) choose=true;
    }
    if(choose) {
        g=choose_camp(t);
        if(!t.searches) return;
        t.memory.claim(g);t.memory.goal_since=int(t.round);t.memory.stalled=0;
        t.memory.no_task_pending=g<0;
        // Selecting a task already computed a full map distance field. Execute
        // travel OR the no-task recovery with a fresh budget next turn. Persist
        // failure explicitly so repeated failed selection cannot starve recovery.
        return;
    }
    auto& r=t.memory.groups[g];
    if(t.memory.stalled>16&&!r.reward) {
        r.avoid_until=int(t.round)+25;t.memory.claim(-1);t.memory.stalled=0;return;
    }
    if(r.reward) {
        auto roots=r.group.spawn_tiles();
        const bc::champion* occupier=nullptr;
        for(const auto& e:t.enemies) if(e.is_visible()&&e.is_alive()&&roots.is_set(e.position())) {
            if(!occupier||e.health()<occupier->health()) occupier=&e;
        }
        if(occupier) {
            // Extra bodies never accelerate a capture; an enemy anywhere in
            // the footprint prevents it completely. Resolve the contest.
            if(t.me.health()<t.incoming(t.me.position(),false)*4.0f) {
                r.avoid_until=int(t.round)+20;t.memory.claim(-1);
                t.memory.going_home=true;recover(t);return;
            }
            if(!fight(t,*occupier)&&t.me.can_move()) {
                navigate(t,approach_roots(t,occupier->position(),t.me.attack_range()));
                if(t.me.attack_range()==1&&t.budget(6000)) fight(t,*occupier);
            }
            t.memory.stalled=0;return;
        }
        if(roots.is_set(t.me.position())) {
            if(t.memory.stalled<=14) return;
            // A legal uncontested capture takes only a few round starts. If no
            // reward arrives, leave this task rather than waiting indefinitely
            // for a hidden or distant contest to resolve itself.
            r.avoid_until=int(t.round)+20;t.memory.claim(-1);
            auto outside=approach_roots(t,t.me.position(),3);outside &= ~roots;
            navigate(t,std::move(outside));return;
        }
        if(t.memory.stalled>20) {r.avoid_until=int(t.round)+15;t.memory.claim(-1);return;}
        for(const auto& a:t.friends) if(id(a)!=id(t.me)&&a.is_visible()&&a.is_alive()&&roots.is_set(a.position())) {
            if(id(a)<id(t.me)) {r.avoid_until=int(t.round)+8;t.memory.claim(-1);}
            // Give a higher-slot helper one turn to leave this footprint.
            return;
        }
        navigate(t,std::move(roots));return;
    }
    const bc::camp* target=nullptr; float best=-1;
    for(const auto& c:r.group) {
        if(!t.budget(14000)) break;
        if(!c.is_visible()||!c.is_alive()) continue;
        float value=(c.strength()+15.0f)/(c.health()+100.0f)/(1.0f+float(std::max(0,dist(t.me.position(),c.position())-t.me.attack_range()))*0.3f);
        if(value>best) {best=value;target=&c;}
    }
    if(!target) {
        auto roots=approach_roots(t,r.centre,1);
        if(r.next_ready>0) roots &= ~r.group.spawn_tiles();
        place_ward(t,r.centre);navigate(t,std::move(roots));return;
    }
    float threat=t.incoming(t.me.position());
    if(threat>0&&t.me.health()<threat*3.0f+50.0f) {t.memory.going_home=true;recover(t);return;}
    bool acted=fight(t,*target);
    if(acted) t.memory.stalled=0;
    if(target->is_visible()&&target->is_alive()&&t.me.can_move()) {
        auto p=target->position();
        if(dist(t.me.position(),p)>t.me.attack_range()) {
            place_ward(t,p);
            navigate(t,approach_roots(t,p,t.me.attack_range()));
            if(t.me.attack_range()==1&&t.budget(6500)) fight(t,*target);
        }
    } else if(!target->is_visible()&&t.me.can_move()) {
        // Moving onto the cleared group is legal only after all occupants vanish.
        if(t.budget(7000)) {observe_group(t,r);if(r.reward) navigate(t,r.group.spawn_tiles());}
    }
}
inline void draft() {
    using D=bc::champion_draft;
    std::array<D,12> order={D::vesper_vale,D::varg,D::marcella,D::tavi,D::ryth,D::vendetta,D::heart_of_ruin,D::ren,D::prism,D::stellar,D::willow,D::cassi_and_issac};
    // Maps with many adjacent camps reward Varg's splash from the first pick.
    int large=0,total=0;
    for(const auto& g:bc::map::camp_groups()) {++total;if(g.camps().size()>=3)++large;}
    if(large*3>total) std::swap(order[0],order[1]);
    auto picked=bc::draft::current_draft(bc::team::ally);
    std::array<std::uint8_t,20> plan{};
    plan[0]='E';plan[1]='S';plan[2]='T';plan[3]=1;
    plan[4]=42;plan[5]=std::uint8_t(bc::draft::team_size());
    for(int i=0;i<12;++i) plan[8+i]=std::uint8_t(order[i]);
    bc::draft_memory=plan;
    for(D d:order) if(std::find(picked.begin(),picked.end(),d)==picked.end()) {bc::draft::pick(d);return;}
}
inline void play(Memory& m) {
    auto& me=bc::self();
    if(!m.initialized) m.initialize();
    if(!me.is_alive()) {m.claim(-1);m.going_home=false;m.bridge_active=false;m.no_task_pending=false;m.recovery_delivery_attempted=false;m.home_walk_failed=false;m.delivery_failures=0;shop(me,m.transport_required);return;}
    shop(me,m.transport_required);
    if(me.is_recalling()||me.is_submitting_gems()||me.is_teleporting()) {m.claim(-1);m.bridge_active=false;return;}
    if(!me.can_move()&&!me.can_attack()&&!me.can_cast()) return;
    Turn t(m);
    P here=me.position();
    unsigned gold=me.gold_count(),gems=me.gem_count();
    if(here!=m.previous||gold!=m.last_gold||gems!=m.last_gems) m.last_progress_round=t.round;
    m.last_gold=gold;m.last_gems=gems;
    if(here==m.previous) ++m.stalled; else m.stalled=0;
    m.previous=here;m.round=t.round;
    // Task replacement is not physical progress. Repeatedly choosing different
    // unreachable islands must eventually recover, even when each new choice
    // resets its local timeout. Actual combat updates this clock in fight().
    if(t.round>m.last_progress_round+24&&!bc::map::spawn_region(bc::team::ally).is_set(here)) {
        m.claim(-1);m.bridge_active=false;m.going_home=true;
    }
    if(m.no_task_pending) {
        m.no_task_pending=false;
        if(!bc::map::spawn_region(bc::team::ally).is_set(here)) t.memory.going_home=true;
    }
    if(recover(t)||t.searches||!t.budget(35000)) return;
    support(t);
    if(t.incoming(here,false)>me.health()*0.22f) {m.going_home=true;recover(t);return;}
    if(deliver(t)||t.searches||!t.budget(35000)) return;
    if(enemy_opportunity(t)) return;
    work_camp(t);
}
}
