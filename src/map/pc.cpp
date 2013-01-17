#include "pc.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../common/db.hpp"
#include "../common/mt_rand.hpp"
#include "../common/nullpo.hpp"
#include "../common/socket.hpp"
#include "../common/timer.hpp"

#include "atcommand.hpp"
#include "battle.hpp"
#include "chat.hpp"
#include "chrif.hpp"
#include "clif.hpp"
#include "intif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "npc.hpp"
#include "party.hpp"
#include "script.hpp"
#include "skill.hpp"
#include "storage.hpp"
#include "trade.hpp"

#include "../poison.hpp"

#define PVP_CALCRANK_INTERVAL 1000  // PVP順位計算の間隔

//define it here, since the ifdef only occurs in this file
#define USE_ASTRAL_SOUL_SKILL

#ifdef USE_ASTRAL_SOUL_SKILL
#define MAGIC_SKILL_THRESHOLD 200   // [fate] At this threshold, the Astral Soul skill kicks in
#endif

#define MAP_LOG_STATS(sd, suffix)       \
        MAP_LOG_PC(sd, "STAT %d %d %d %d %d %d " suffix,            \
                   sd->status.attrs[ATTR::STR], sd->status.attrs[ATTR::AGI], sd->status.attrs[ATTR::VIT], sd->status.attrs[ATTR::INT], sd->status.attrs[ATTR::DEX], sd->status.attrs[ATTR::LUK])

#define MAP_LOG_XP(sd, suffix)  \
        MAP_LOG_PC(sd, "XP %d %d JOB %d %d %d ZENY %d + %d " suffix,            \
                   sd->status.base_level, sd->status.base_exp, sd->status.job_level, sd->status.job_exp, sd->status.skill_point,  sd->status.zeny, pc_readaccountreg(sd, "BankAccount"))

#define MAP_LOG_MAGIC(sd, suffix)       \
        MAP_LOG_PC(sd, "MAGIC %d %d %d %d %d %d EXP %d %d " suffix,     \
                   sd->status.skill[TMW_MAGIC].lv,                      \
                   sd->status.skill[TMW_MAGIC_LIFE].lv,                 \
                   sd->status.skill[TMW_MAGIC_WAR].lv,                  \
                   sd->status.skill[TMW_MAGIC_TRANSMUTE].lv,            \
                   sd->status.skill[TMW_MAGIC_NATURE].lv,               \
                   sd->status.skill[TMW_MAGIC_ETHER].lv,                \
                   pc_readglobalreg(sd, "MAGIC_EXPERIENCE") & 0xffff,   \
                   (pc_readglobalreg(sd, "MAGIC_EXPERIENCE") >> 24) & 0xff)

timer_id day_timer_tid;
timer_id night_timer_tid;

static //const
int max_weight_base_0 = 20000;
static //const
int hp_coefficient_0 = 0;
static //const
int hp_coefficient2_0 = 500;
// TODO see if this can be turned into an "as-needed" formula
static
int hp_sigma_val_0[MAX_LEVEL];
static //const
int sp_coefficient_0 = 100;

// coefficients for each weapon type
// (not all used)
static //const
int aspd_base_0[17] =
{
    650,
    700,
    750,
    600,
    2000,
    2000,
    800,
    2000,
    700,
    700,
    650,
    900,
    2000,
    2000,
    2000,
    2000,
    2000,
};
static const
int exp_table_0[MAX_LEVEL] =
{
    // 1 .. 9
                9,          16,         25,         36,
    77,         112,        153,        200,        253,
    // 10 .. 19
    320,        385,        490,        585,        700,
    830,        970,        1120,       1260,       1420,
    // 20 .. 29
    1620,       1860,       1990,       2240,       2504,
    2950,       3426,       3934,       4474,       6889,
    // 30 .. 39
    7995,       9174,       10425,      11748,      13967,
    15775,      17678,      19677,      21773,      30543,
    // 40 .. 49
    34212,      38065,      42102,      46323,      53026,
    58419,      64041,      69892,      75973,      102468,
    // 50 .. 59
    115254,     128692,     142784,     157528,     178184,
    196300,     215198,     234879,     255341,     330188,
    // 60 .. 69
    365914,     403224,     442116,     482590,     536948,
    585191,     635278,     687211,     740988,     925400,
    // 70 .. 79
    1473746,    1594058,    1718928,    1848355,    1982340,
    2230113,    2386162,    2547417,    2713878,    3206160,
    // 80 .. 89
    3681024,    4022472,    4377024,    4744680,    5125440,
    5767272,    6204000,    6655464,    7121664,    7602600,
    // 90 .. 99
    9738720,    11649960,   13643520,   18339300,   23836800,
    35658000,   48687000,   58135000,   99999999,   0,
};
// is this *actually* used anywhere?
static const
int exp_table_7[MAX_LEVEL] =
{
    // 1 .. 9
        10, 18, 28, 40, 91, 151, 205, 268, 340
};
// TODO generate this table instead
static int stat_p[MAX_LEVEL] =
{
    // 1..9
        48, 52, 56, 60,         64, 69, 74, 79, 84,
    // 10..19
    90, 96, 102,108,115,        122,129,136,144,152,
    // 20..29
    160,168,177,186,195,        204,214,224,234,244,
    // 30..39
    255,266,277,288,300,        312,324,336,349,362,
    // 40..49
    375,388,402,416,430,        444,459,474,489,504,
    // 50..59
    520,536,552,568,585,        602,619,636,654,672,
    // 60..69
    690,708,727,746,765,        784,804,824,844,864,
    // 70..79
    885,906,927,948,970,        992,1014,1036,1059,1082,
    // 80..89
    1105,1128,1152,1176,1200,   1224,1249,1274,1299,1324,
    // 90..99
    1350,1376,1402,1428,1455,   1482,1509,1536,1564,1592,
    // 100..109
    1620,1648,1677,1706,1735,   1764,1794,1824,1854,1884,
    // 110..119
    1915,1946,1977,2008,2040,   2072,2104,2136,2169,2202,
    // 120..129
    2235,2268,2302,2336,2370,   2404,2439,2474,2509,2544,
    // 130..139
    2580,2616,2652,2688,2725,   2762,2799,2836,2874,2912,
    // 140..149
    2950,2988,3027,3066,3105,   3144,3184,3224,3264,3304,
    // 150..159
    3345,3386,3427,3468,3510,   3552,3594,3636,3679,3722,
    // 160..169
    3765,3808,3852,3896,3940,   3984,4029,4074,4119,4164,
    // 170..179
    4210,4256,4302,4348,4395,   4442,4489,4536,4584,4632,
    // 180..189
    4680,4728,4777,4826,4875,   4924,4974,5024,5074,5124,
    // 190..199
    5175,5226,5277,5328,5380,   5432,5484,5536,5589,5642,
    // 200..209
    5695,5748,5802,5856,5910,   5964,6019,6074,6129,6184,
    // 210..219
    6240,6296,6352,6408,6465,   6522,6579,6636,6694,6752,
    // 220..229
    6810,6868,6927,6986,7045,   7104,7164,7224,7284,7344,
    // 230..239
    7405,7466,7527,7588,7650,   7712,7774,7836,7899,7962,
    // 240..249
    8025,8088,8152,8216,8280,   8344,8409,8474,8539,8604,
    // 250..255
    8670,8736,8802,8868,8935,   9002,
};

static
int dirx[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };
static
int diry[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };

static
earray<EPOS, EQUIP, EQUIP::COUNT> equip_pos //=
{{
    EPOS::MISC2,
    EPOS::CAPE,
    EPOS::SHOES,
    EPOS::GLOVES,
    EPOS::LEGS,
    EPOS::TORSO,
    EPOS::HAT,
    EPOS::MISC1,
    EPOS::SHIELD,
    EPOS::WEAPON,
    EPOS::ARROW,
}};

//static struct dbt *gm_account_db;
static
struct gm_account *gm_account = NULL;
static
int GM_num = 0;

static
int pc_checkoverhp(struct map_session_data *sd);
static
int pc_checkoversp(struct map_session_data *sd);
static
int pc_nextbaseafter(struct map_session_data *sd);
static
int pc_nextjobafter(struct map_session_data *sd);
static
void pc_setdead(struct map_session_data *sd)
{
    sd->state.dead_sit = 1;
}

int pc_isGM(struct map_session_data *sd)
{
//  struct gm_account *p;
    int i;

    nullpo_ret(sd);

/*      p = numdb_search(gm_account_db, sd->status.account_id);
        if (p == NULL)
                return 0;
        return p->level;*/

    for (i = 0; i < GM_num; i++)
        if (gm_account[i].account_id == sd->status.account_id)
            return gm_account[i].level;
    return 0;

}

int pc_iskiller(struct map_session_data *src,
                 struct map_session_data *target)
{
    nullpo_ret(src);

    if (src->bl.type != BL_PC)
        return 0;
    if (src->special_state.killer)
        return 1;

    if (target->bl.type != BL_PC)
        return 0;
    if (target->special_state.killable)
        return 1;

    return 0;
}

int pc_set_gm_level(int account_id, int level)
{
    int i;
    for (i = 0; i < GM_num; i++)
    {
        if (account_id == gm_account[i].account_id)
        {
            gm_account[i].level = level;
            return 0;
        }
    }

    GM_num++;
    RECREATE(gm_account, struct gm_account, GM_num);
    gm_account[GM_num - 1].account_id = account_id;
    gm_account[GM_num - 1].level = level;
    return 0;
}

static
int distance(int x0, int y0, int x1, int y1)
{
    int dx, dy;

    dx = abs(x0 - x1);
    dy = abs(y0 - y1);
    return dx > dy ? dx : dy;
}

static
void pc_invincible_timer(timer_id tid, tick_t, custom_id_t id, custom_data_t)
{
    struct map_session_data *sd;

    if ((sd = map_id2sd(id)) == NULL
        || sd->bl.type != BL_PC)
        return;

    if (sd->invincible_timer != tid)
    {
        if (battle_config.error_log)
            PRINTF("invincible_timer %d != %d\n", sd->invincible_timer, tid);
        return;
    }
    sd->invincible_timer = -1;
}

int pc_setinvincibletimer(struct map_session_data *sd, int val)
{
    nullpo_ret(sd);

    if (sd->invincible_timer != -1)
        delete_timer(sd->invincible_timer, pc_invincible_timer);
    sd->invincible_timer =
        add_timer(gettick() + val, pc_invincible_timer, sd->bl.id, 0);
    return 0;
}

int pc_delinvincibletimer(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->invincible_timer != -1)
    {
        delete_timer(sd->invincible_timer, pc_invincible_timer);
        sd->invincible_timer = -1;
    }
    return 0;
}

static
void pc_spiritball_timer(timer_id tid, tick_t, custom_id_t id, custom_data_t)
{
    struct map_session_data *sd;
    int i;

    if ((sd = map_id2sd(id)) == NULL
        || sd->bl.type != BL_PC)
        return;

    if (sd->spirit_timer[0] != tid)
    {
        if (battle_config.error_log)
            PRINTF("spirit_timer %d != %d\n", sd->spirit_timer[0], tid);
        return;
    }
    sd->spirit_timer[0] = -1;
    for (i = 1; i < sd->spiritball; i++)
    {
        sd->spirit_timer[i - 1] = sd->spirit_timer[i];
        sd->spirit_timer[i] = -1;
    }
    sd->spiritball--;
    if (sd->spiritball < 0)
        sd->spiritball = 0;
}

int pc_addspiritball(struct map_session_data *sd, int interval, int max)
{
    int i;

    nullpo_ret(sd);

    if (max > MAX_SKILL_LEVEL)
        max = MAX_SKILL_LEVEL;
    if (sd->spiritball < 0)
        sd->spiritball = 0;

    if (sd->spiritball >= max)
    {
        if (sd->spirit_timer[0] != -1)
        {
            delete_timer(sd->spirit_timer[0], pc_spiritball_timer);
            sd->spirit_timer[0] = -1;
        }
        for (i = 1; i < max; i++)
        {
            sd->spirit_timer[i - 1] = sd->spirit_timer[i];
            sd->spirit_timer[i] = -1;
        }
    }
    else
        sd->spiritball++;

    sd->spirit_timer[sd->spiritball - 1] =
        add_timer(gettick() + interval, pc_spiritball_timer, sd->bl.id, 0);

    return 0;
}

int pc_delspiritball(struct map_session_data *sd, int count, int)
{
    int i;

    nullpo_ret(sd);

    if (sd->spiritball <= 0)
    {
        sd->spiritball = 0;
        return 0;
    }

    if (count > sd->spiritball)
        count = sd->spiritball;
    sd->spiritball -= count;
    if (count > MAX_SKILL_LEVEL)
        count = MAX_SKILL_LEVEL;

    for (i = 0; i < count; i++)
    {
        if (sd->spirit_timer[i] != -1)
        {
            delete_timer(sd->spirit_timer[i], pc_spiritball_timer);
            sd->spirit_timer[i] = -1;
        }
    }
    for (i = count; i < MAX_SKILL_LEVEL; i++)
    {
        sd->spirit_timer[i - count] = sd->spirit_timer[i];
        sd->spirit_timer[i] = -1;
    }

    return 0;
}

int pc_setrestartvalue(struct map_session_data *sd, int type)
{
    nullpo_ret(sd);

    //-----------------------
    // 死亡した
    if (sd->special_state.restart_full_recover)
    {                           // オシリスカード
        sd->status.hp = sd->status.max_hp;
        sd->status.sp = sd->status.max_sp;
    }
    else
    {
        if (battle_config.restart_hp_rate < 50)
            sd->status.hp = (sd->status.max_hp) / 2;
        else
        {
            if (battle_config.restart_hp_rate <= 0)
                sd->status.hp = 1;
            else
            {
                sd->status.hp =
                    sd->status.max_hp * battle_config.restart_hp_rate / 100;
                if (sd->status.hp <= 0)
                    sd->status.hp = 1;
            }
        }
        if (battle_config.restart_sp_rate > 0)
        {
            int sp = sd->status.max_sp * battle_config.restart_sp_rate / 100;
            if (sd->status.sp < sp)
                sd->status.sp = sp;
        }
    }
    if (type & 1)
        clif_updatestatus(sd, SP_HP);
    if (type & 1)
        clif_updatestatus(sd, SP_SP);

    sd->heal_xp = 0;            // [Fate] Set gainable xp for healing this player to 0

    return 0;
}

/*==========================================
 * 自分をロックしているMOBの数を数える(foreachclient)
 *------------------------------------------
 */
static
void pc_counttargeted_sub(struct block_list *bl,
        int id, int *c, struct block_list *src, ATK target_lv)
{
    nullpo_retv(bl);

    if (id == bl->id || (src && id == src->id))
        return;
    if (bl->type == BL_PC)
    {
        struct map_session_data *sd = (struct map_session_data *) bl;
        if (sd && sd->attacktarget == id && sd->attacktimer != -1
            && sd->attacktarget_lv >= target_lv)
            (*c)++;
    }
    else if (bl->type == BL_MOB)
    {
        struct mob_data *md = (struct mob_data *) bl;
        if (md && md->target_id == id && md->timer != -1
            && md->state.state == MS_ATTACK && md->target_lv >= target_lv)

            (*c)++;
        //PRINTF("md->target_lv:%d, target_lv:%d\n",((struct mob_data *)bl)->target_lv,target_lv);
    }
}

int pc_counttargeted(struct map_session_data *sd, struct block_list *src,
        ATK target_lv)
{
    int c = 0;
    map_foreachinarea(std::bind(pc_counttargeted_sub, ph::_1, sd->bl.id, &c, src, target_lv),
            sd->bl.m, sd->bl.x - AREA_SIZE, sd->bl.y - AREA_SIZE,
            sd->bl.x + AREA_SIZE, sd->bl.y + AREA_SIZE, BL_NUL);
    return c;
}

/*==========================================
 * ローカルプロトタイプ宣言 (必要な物のみ)
 *------------------------------------------
 */
static
int pc_walktoxy_sub(struct map_session_data *);

/*==========================================
 * saveに必要なステータス修正を行なう
 *------------------------------------------
 */
int pc_makesavestatus(struct map_session_data *sd)
{
    nullpo_ret(sd);

    // 服の色は色々弊害が多いので保存対象にはしない
    if (!battle_config.save_clothcolor)
        sd->status.clothes_color = 0;

    // 死亡状態だったのでhpを1、位置をセーブ場所に変更
    if (pc_isdead(sd))
    {
        pc_setrestartvalue(sd, 0);
        memcpy(&sd->status.last_point, &sd->status.save_point,
                sizeof(sd->status.last_point));
    }
    else
    {
        memcpy(sd->status.last_point.map, sd->mapname, 24);
        sd->status.last_point.x = sd->bl.x;
        sd->status.last_point.y = sd->bl.y;
    }

    // セーブ禁止マップだったので指定位置に移動
    if (map[sd->bl.m].flag.nosave)
    {
        struct map_data *m = &map[sd->bl.m];
        if (strcmp(m->save.map, "SavePoint") == 0)
            memcpy(&sd->status.last_point, &sd->status.save_point,
                    sizeof(sd->status.last_point));
        else
            memcpy(&sd->status.last_point, &m->save,
                    sizeof(sd->status.last_point));
    }

    //マナーポイントがプラスだった場合0に
    if (battle_config.muting_players && sd->status.manner > 0)
        sd->status.manner = 0;
    return 0;
}

/*==========================================
 * 接続時の初期化
 *------------------------------------------
 */
int pc_setnewpc(struct map_session_data *sd, int account_id, int char_id,
                 int login_id1, int client_tick, int sex, int)
{
    nullpo_ret(sd);

    sd->bl.id = account_id;
    sd->char_id = char_id;
    sd->login_id1 = login_id1;
    sd->login_id2 = 0;          // at this point, we can not know the value :(
    sd->client_tick = client_tick;
    sd->sex = sex;
    sd->state.auth = 0;
    sd->bl.type = BL_PC;
    sd->canact_tick = sd->canmove_tick = gettick();
    sd->canlog_tick = gettick();
    sd->state.waitingdisconnect = 0;

    return 0;
}

EPOS pc_equippoint(struct map_session_data *sd, int n)
{
    nullpo_retr(EPOS::ZERO, sd);

    if (!sd->inventory_data[n])
        return EPOS::ZERO;

    EPOS ep = sd->inventory_data[n]->equip;

    return ep;
}

static
int pc_setinventorydata(struct map_session_data *sd)
{
    int i, id;

    nullpo_ret(sd);

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        id = sd->status.inventory[i].nameid;
        sd->inventory_data[i] = itemdb_search(id);
    }
    return 0;
}

static
int pc_calcweapontype(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->weapontype1 != 0 && sd->weapontype2 == 0)
        sd->status.weapon = sd->weapontype1;
    if (sd->weapontype1 == 0 && sd->weapontype2 != 0)   // 左手武器 Only
        sd->status.weapon = sd->weapontype2;
    else if (sd->weapontype1 == 1 && sd->weapontype2 == 1)  // 双短剣
        sd->status.weapon = 0x11;
    else if (sd->weapontype1 == 2 && sd->weapontype2 == 2)  // 双単手剣
        sd->status.weapon = 0x12;
    else if (sd->weapontype1 == 6 && sd->weapontype2 == 6)  // 双単手斧
        sd->status.weapon = 0x13;
    else if ((sd->weapontype1 == 1 && sd->weapontype2 == 2) || (sd->weapontype1 == 2 && sd->weapontype2 == 1))  // 短剣 - 単手剣
        sd->status.weapon = 0x14;
    else if ((sd->weapontype1 == 1 && sd->weapontype2 == 6) || (sd->weapontype1 == 6 && sd->weapontype2 == 1))  // 短剣 - 斧
        sd->status.weapon = 0x15;
    else if ((sd->weapontype1 == 2 && sd->weapontype2 == 6) || (sd->weapontype1 == 6 && sd->weapontype2 == 2))  // 単手剣 - 斧
        sd->status.weapon = 0x16;
    else
        sd->status.weapon = sd->weapontype1;

    return 0;
}

static
int pc_setequipindex(struct map_session_data *sd)
{
    nullpo_ret(sd);

    for (EQUIP i : EQUIPs)
        sd->equip_index[i] = -1;

    for (int i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid <= 0)
            continue;
        if (bool(sd->status.inventory[i].equip))
        {
            for (EQUIP j : EQUIPs)
                if (bool(sd->status.inventory[i].equip & equip_pos[j]))
                    sd->equip_index[j] = i;
            if (bool(sd->status.inventory[i].equip & EPOS::WEAPON))
            {
                if (sd->inventory_data[i])
                    sd->weapontype1 = sd->inventory_data[i]->look;
                else
                    sd->weapontype1 = 0;
            }
            if (bool(sd->status.inventory[i].equip & EPOS::SHIELD))
            {
                if (sd->inventory_data[i])
                {
                    if (sd->inventory_data[i]->type == ItemType::WEAPON)
                    {
                        if (sd->status.inventory[i].equip == EPOS::SHIELD)
                            sd->weapontype2 = sd->inventory_data[i]->look;
                        else
                            sd->weapontype2 = 0;
                    }
                    else
                        sd->weapontype2 = 0;
                }
                else
                    sd->weapontype2 = 0;
            }
        }
    }
    pc_calcweapontype(sd);

    return 0;
}

static
int pc_isequip(struct map_session_data *sd, int n)
{
    struct item_data *item;
    eptr<struct status_change, StatusChange> sc_data;
    //転生や養子の場合の元の職業を算出する

    nullpo_ret(sd);

    item = sd->inventory_data[n];
    sc_data = battle_get_sc_data(&sd->bl);

    if (battle_config.gm_allequip > 0
        && pc_isGM(sd) >= battle_config.gm_allequip)
        return 1;

    if (item == NULL)
        return 0;
    if (item->sex != 2 && sd->status.sex != item->sex)
        return 0;
    if (item->elv > 0 && sd->status.base_level < item->elv)
        return 0;

    if (map[sd->bl.m].flag.pvp
        && (item->flag.no_equip == 1 || item->flag.no_equip == 3))
        return 0;
    if (bool(item->equip & EPOS::WEAPON)
        && sc_data
        && sc_data[SC_STRIPWEAPON].timer != -1)
        return 0;
    if (bool(item->equip & EPOS::SHIELD)
        && sc_data
        && sc_data[SC_STRIPSHIELD].timer != -1)
        return 0;
    if (bool(item->equip & EPOS::MISC1)
        && sc_data
        && sc_data[SC_STRIPARMOR].timer != -1)
        return 0;
    if (bool(item->equip & EPOS::HAT)
        && sc_data
        && sc_data[SC_STRIPHELM].timer != -1)
        return 0;
    return 1;
}

/*==========================================
 * Weapon Breaking [Valaris]
 *------------------------------------------
 */
int pc_breakweapon(struct map_session_data *sd)
{
    struct item_data *item;
    int i;

    if (sd == NULL)
        return -1;
    if (sd->unbreakable >= MRAND(100))
        return 0;
    if (sd->sc_data[SC_CP_WEAPON].timer != -1)
        return 0;

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (bool(sd->status.inventory[i].equip)
            && bool(sd->status.inventory[i].equip & EPOS::WEAPON)
            && !sd->status.inventory[i].broken)
        {
            item = sd->inventory_data[i];
            sd->status.inventory[i].broken = 1;
            //pc_unequipitem(sd,i,CalcStatus::NOW);
            if (bool(sd->status.inventory[i].equip)
                && bool(sd->status.inventory[i].equip & EPOS::WEAPON)
                && sd->status.inventory[i].broken == 1)
            {
                std::string output = STRPRINTF("%s has broken.", item->jname);
                clif_emotion(&sd->bl, 23);
                clif_displaymessage(sd->fd, output);
                clif_equiplist(sd);
                skill_status_change_start(&sd->bl, SC_BROKNWEAPON, 0, 0, 0,
                                           0, 0, 0);
            }
        }
        if (sd->status.inventory[i].broken == 1)
            return 0;
    }

    return 0;
}

/*==========================================
 * Armor Breaking [Valaris]
 *------------------------------------------
 */
int pc_breakarmor(struct map_session_data *sd)
{
    if (sd == NULL)
        return -1;
    if (sd->unbreakable >= MRAND(100))
        return 0;
    if (sd->sc_data[SC_CP_ARMOR].timer != -1)
        return 0;

    for (int i = 0; i < MAX_INVENTORY; i++)
    {
        if (bool(sd->status.inventory[i].equip)
            && bool(sd->status.inventory[i].equip & EPOS::MISC1)
            && !sd->status.inventory[i].broken)
        {
            struct item_data *item = sd->inventory_data[i];
            sd->status.inventory[i].broken = 1;
            if (bool(sd->status.inventory[i].equip)
                && bool(sd->status.inventory[i].equip & EPOS::MISC1)
                && sd->status.inventory[i].broken == 1)
            {
                std::string output = STRPRINTF("%s has broken.",
                        item->jname);
                clif_emotion(&sd->bl, 23);
                clif_displaymessage(sd->fd, output);
                clif_equiplist(sd);
                skill_status_change_start(&sd->bl, SC_BROKNARMOR, 0, 0, 0, 0,
                                           0, 0);
            }
        }
        if (sd->status.inventory[i].broken == 1)
            return 0;
    }
    return 0;
}

/*==========================================
 * session idに問題無し
 * char鯖から送られてきたステータスを設定
 *------------------------------------------
 */
int pc_authok(int id, int login_id2, time_t connect_until_time,
               short tmw_version, const struct mmo_charstatus *st)
{
    struct map_session_data *sd = NULL;

    struct party *p;
    unsigned long tick = gettick();
    struct sockaddr_in sai;
    socklen_t sa_len = sizeof(struct sockaddr);

    sd = map_id2sd(id);
    if (sd == NULL)
        return 1;

    sd->login_id2 = login_id2;
    sd->tmw_version = tmw_version;

    memcpy(&sd->status, st, sizeof(*st));

    if (sd->status.sex != sd->sex)
    {
        clif_authfail_fd(sd->fd, 0);
        return 1;
    }

    MAP_LOG_STATS(sd, "LOGIN");
    MAP_LOG_XP(sd, "LOGIN");
    MAP_LOG_MAGIC(sd, "LOGIN");

    memset(&sd->state, 0, sizeof(sd->state));
    // 基本的な初期化
    sd->state.connect_new = 1;
    sd->bl.prev = sd->bl.next = NULL;

    sd->weapontype1 = sd->weapontype2 = 0;
    sd->speed = DEFAULT_WALK_SPEED;
    sd->state.dead_sit = 0;
    sd->dir = 0;
    sd->head_dir = 0;
    sd->state.auth = 1;
    sd->walktimer = -1;
    sd->attacktimer = -1;
    sd->followtimer = -1;       // [MouseJstr]
    sd->skilltimer = -1;
    sd->skillitem = SkillID::NEGATIVE;
    sd->skillitemlv = -1;
    sd->invincible_timer = -1;
    sd->sg_count = 0;

    sd->deal_locked = 0;
    sd->trade_partner = 0;

    sd->inchealhptick = 0;
    sd->inchealsptick = 0;
    sd->hp_sub = 0;
    sd->sp_sub = 0;
    sd->quick_regeneration_hp.amount = 0;
    sd->quick_regeneration_sp.amount = 0;
    sd->heal_xp = 0;
    sd->inchealspirithptick = 0;
    sd->inchealspiritsptick = 0;
    sd->canact_tick = tick;
    sd->canmove_tick = tick;
    sd->attackabletime = tick;
    /* We don't want players bypassing spell restrictions. [remoitnane] */
    // Removed because it was buggy with the ~50 day wraparound,
    // and there's already a limit on how fast you can log in and log out.
    // -o11c
    sd->cast_tick = tick; // + pc_readglobalreg (sd, "MAGIC_CAST_TICK");

    sd->doridori_counter = 0;

    sd->spiritball = 0;
    for (int i = 0; i < MAX_SKILL_LEVEL; i++)
        sd->spirit_timer[i] = -1;
    for (int i = 0; i < MAX_SKILLTIMERSKILL; i++)
        sd->skilltimerskill[i].timer = -1;

    memset(&sd->dev, 0, sizeof(struct square));
    for (int i = 0; i < 5; i++)
    {
        sd->dev.val1[i] = 0;
        sd->dev.val2[i] = 0;
    }

    // アカウント変数の送信要求
    intif_request_accountreg(sd);

    // アイテムチェック
    pc_setinventorydata(sd);
    pc_checkitem(sd);

    // ステータス異常の初期化
    for (StatusChange i : erange(StatusChange(), MAX_STATUSCHANGE))
    {
        sd->sc_data[i].timer = -1;
        sd->sc_data[i].val1 = sd->sc_data[i].val2 = sd->sc_data[i].val3 =
            sd->sc_data[i].val4 = 0;
    }
    sd->sc_count = 0;
    if ((battle_config.atc_gmonly == 0 || pc_isGM(sd)) &&
        (pc_isGM(sd) >= get_atcommand_level(AtCommand_Hide)))
        sd->status.option &= (Option::MASK | Option::HIDE);
    else
        sd->status.option &= Option::MASK;

    // スキルユニット関係の初期化
    memset(sd->skillunit, 0, sizeof(sd->skillunit));
    memset(sd->skillunittick, 0, sizeof(sd->skillunittick));

    // init ignore list
    memset(sd->ignore, 0, sizeof(sd->ignore));

    // パーティー関係の初期化
    sd->party_sended = 0;
    sd->party_invite = 0;
    sd->party_x = -1;
    sd->party_y = -1;
    sd->party_hp = -1;

    // イベント関係の初期化
    memset(sd->eventqueue, 0, sizeof(sd->eventqueue));
    for (int i = 0; i < MAX_EVENTTIMER; i++)
        sd->eventtimer[i] = -1;

    // 位置の設定
    pc_setpos(sd, sd->status.last_point.map, sd->status.last_point.x,
               sd->status.last_point.y, 0);

    // パーティ、ギルドデータの要求
    if (sd->status.party_id > 0
        && (p = party_search(sd->status.party_id)) == NULL)
        party_request_info(sd->status.party_id);

    // pvpの設定
    sd->pvp_rank = 0;
    sd->pvp_point = 0;
    sd->pvp_timer = -1;

    // 通知

    clif_authok(sd);
    map_addnickdb(sd);
    if (map_charid2nick(sd->status.char_id) == NULL)
        map_addchariddb(sd->status.char_id, sd->status.name);

    //スパノビ用死にカウンターのスクリプト変数からの読み出しとsdへのセット
    sd->die_counter = pc_readglobalreg(sd, "PC_DIE_COUNTER");

    if (night_flag == 1)
    {
        char tmpstr[1024];
        strcpy(tmpstr, "Actually, it's the night...");
        clif_wis_message(sd->fd, wisp_server_name, tmpstr,
                          strlen(tmpstr) + 1);
        sd->opt2 |= Opt2::BLIND;
    }

    // ステータス初期計算など
    pc_calcstatus(sd, 1);

    if (pc_isGM(sd))
    {
        PRINTF("Connection accepted: character '%s' (account: %d; GM level %d).\n",
             sd->status.name, sd->status.account_id, pc_isGM(sd));
        clif_updatestatus(sd, SP_GM);
    }
    else
        PRINTF("Connection accepted: Character '%s' (account: %d).\n",
                sd->status.name, sd->status.account_id);

    // Message of the Dayの送信
    {
        char buf[256];
        FILE *fp;
        if ((fp = fopen_(motd_txt, "r")) != NULL)
        {
            while (fgets(buf, sizeof(buf) - 1, fp) != NULL)
            {
                for (int i = 0; buf[i]; i++)
                {
                    if (buf[i] == '\r' || buf[i] == '\n')
                    {
                        buf[i] = 0;
                        break;
                    }
                }
                clif_displaymessage(sd->fd, buf);
            }
            fclose_(fp);
        }
    }

    sd->auto_ban_info.in_progress = 0;

    // Initialize antispam vars
    sd->chat_reset_due = sd->chat_lines_in = sd->chat_total_repeats =
        sd->chat_repeat_reset_due = 0;
    sd->chat_lastmsg[0] = '\0';

    memset(sd->flood_rates, 0, sizeof(sd->flood_rates));
    sd->packet_flood_reset_due = sd->packet_flood_in = 0;

    // Obtain IP address (if they are still connected)
    if (!getpeername(sd->fd, (struct sockaddr *)&sai, &sa_len))
        sd->ip = sai.sin_addr;

    // message of the limited time of the account
    if (connect_until_time != 0)
    {                           // don't display if it's unlimited or unknow value
        char tmpstr[1024];
        strftime(tmpstr, sizeof(tmpstr) - 1, "Your account time limit is: %d-%m-%Y %H:%M:%S.", gmtime(&connect_until_time));
        clif_wis_message(sd->fd, wisp_server_name, tmpstr,
                          strlen(tmpstr) + 1);
    }
    pc_calcstatus(sd, 1);

    return 0;
}

/*==========================================
 * session idに問題ありなので後始末
 *------------------------------------------
 */
int pc_authfail(int id)
{
    struct map_session_data *sd;

    sd = map_id2sd(id);
    if (sd == NULL)
        return 1;

    clif_authfail_fd(sd->fd, 0);

    return 0;
}

static
int pc_calc_skillpoint(struct map_session_data *sd)
{
    int i, skill_points = 0;

    nullpo_ret(sd);

    for (i = 0; i < skill_pool_skills_size; i++) {
        int lv = sd->status.skill[skill_pool_skills[i]].lv;
        if (lv)
            skill_points += ((lv * (lv - 1)) >> 1) - 1;
    }

    return skill_points;
}

/*==========================================
 * 覚えられるスキルの計算
 *------------------------------------------
 */
static
void pc_calc_skilltree(struct map_session_data *sd)
{
    nullpo_retv(sd);

    // TODO - I *think* this can be removed
    // since the skill is worthless without a level
    if (sd->status.skill[NV_EMOTE].id == SkillID::ZERO)
        sd->status.skill[NV_EMOTE].id = NV_EMOTE;
}

/*==========================================
 * 重量アイコンの確認
 *------------------------------------------
 */
int pc_checkweighticon(struct map_session_data *sd)
{
    int flag = 0;

    nullpo_ret(sd);

    if (sd->weight * 2 >= sd->max_weight
        && sd->sc_data[SC_FLYING_BACKPACK].timer == -1)
        flag = 1;
    if (sd->weight * 10 >= sd->max_weight * 9)
        flag = 2;

    if (flag == 1)
    {
        if (sd->sc_data[SC_WEIGHT50].timer == -1)
            skill_status_change_start(&sd->bl, SC_WEIGHT50, 0, 0, 0, 0, 0,
                                       0);
    }
    else
    {
        skill_status_change_end(&sd->bl, SC_WEIGHT50, -1);
    }
    if (flag == 2)
    {
        if (sd->sc_data[SC_WEIGHT90].timer == -1)
            skill_status_change_start(&sd->bl, SC_WEIGHT90, 0, 0, 0, 0, 0,
                                       0);
    }
    else
    {
        skill_status_change_end(&sd->bl, SC_WEIGHT90, -1);
    }
    return 0;
}

static
void pc_set_weapon_look(struct map_session_data *sd)
{
    if (sd->attack_spell_override)
        clif_changelook(&sd->bl, LOOK_WEAPON,
                         sd->attack_spell_look_override);
    else
        clif_changelook(&sd->bl, LOOK_WEAPON, sd->status.weapon);
}

/*==========================================
 * パラメータ計算
 * first==0の時、計算対象のパラメータが呼び出し前から
 * 変 化した場合自動でsendするが、
 * 能動的に変化させたパラメータは自前でsendするように
 *------------------------------------------
 */
int pc_calcstatus(struct map_session_data *sd, int first)
{
    int b_speed, b_max_hp, b_max_sp, b_hp, b_sp, b_weight, b_max_weight,
        b_hit, b_flee;
    int b_aspd, b_watk, b_def, b_watk2, b_def2, b_flee2, b_critical,
        b_attackrange, b_matk1, b_matk2, b_mdef, b_mdef2;
    int b_base_atk;
    earray<struct skill, SkillID, MAX_SKILL> b_skill;
    int bl, index;
    int aspd_rate, wele, wele_, def_ele, refinedef = 0;
    int str, dstr, dex;

    nullpo_ret(sd);

    b_speed = sd->speed;
    b_max_hp = sd->status.max_hp;
    b_max_sp = sd->status.max_sp;
    b_hp = sd->status.hp;
    b_sp = sd->status.sp;
    b_weight = sd->weight;
    b_max_weight = sd->max_weight;
    earray<int, ATTR, ATTR::COUNT> b_paramb = sd->paramb;
    earray<int, ATTR, ATTR::COUNT> b_parame = sd->paramc;
    b_skill = sd->status.skill;
    b_hit = sd->hit;
    b_flee = sd->flee;
    b_aspd = sd->aspd;
    b_watk = sd->watk;
    b_def = sd->def;
    b_watk2 = sd->watk2;
    b_def2 = sd->def2;
    b_flee2 = sd->flee2;
    b_critical = sd->critical;
    b_attackrange = sd->attackrange;
    b_matk1 = sd->matk1;
    b_matk2 = sd->matk2;
    b_mdef = sd->mdef;
    b_mdef2 = sd->mdef2;
    b_base_atk = sd->base_atk;

    pc_calc_skilltree(sd);     // スキルツリーの計算

    sd->max_weight = max_weight_base_0 + sd->status.attrs[ATTR::STR] * 300;

    if (first & 1)
    {
        sd->weight = 0;
        for (int i = 0; i < MAX_INVENTORY; i++)
        {
            if (sd->status.inventory[i].nameid == 0
                || sd->inventory_data[i] == NULL)
                continue;
            sd->weight +=
                sd->inventory_data[i]->weight *
                sd->status.inventory[i].amount;
        }
        sd->cart_max_weight = battle_config.max_cart_weight;
        sd->cart_weight = 0;
        sd->cart_max_num = MAX_CART;
        sd->cart_num = 0;
        for (int i = 0; i < MAX_CART; i++)
        {
            if (sd->status.cart[i].nameid == 0)
                continue;
            sd->cart_weight +=
                itemdb_weight(sd->status.cart[i].nameid) *
                sd->status.cart[i].amount;
            sd->cart_num++;
        }
    }

    for (auto& p : sd->paramb)
        p = 0;
    for (auto& p : sd->parame)
        p = 0;

    sd->hit = 0;
    sd->flee = 0;
    sd->flee2 = 0;
    sd->critical = 0;
    sd->aspd = 0;
    sd->watk = 0;
    sd->def = 0;
    sd->mdef = 0;
    sd->watk2 = 0;
    sd->def2 = 0;
    sd->mdef2 = 0;
    sd->status.max_hp = 0;
    sd->status.max_sp = 0;
    sd->attackrange = 0;
    sd->attackrange_ = 0;
    sd->atk_ele = 0;
    sd->def_ele = 0;
    sd->star = 0;
    sd->overrefine = 0;
    sd->matk1 = 0;
    sd->matk2 = 0;
    sd->speed = DEFAULT_WALK_SPEED;
    sd->hprate = 100;
    sd->sprate = 100;
    sd->castrate = 100;
    sd->dsprate = 100;
    sd->base_atk = 0;
    sd->arrow_atk = 0;
    sd->arrow_ele = 0;
    sd->arrow_hit = 0;
    sd->arrow_range = 0;
    sd->nhealhp = sd->nhealsp = sd->nshealhp = sd->nshealsp = sd->nsshealhp =
        sd->nsshealsp = 0;
    memset(sd->addele, 0, sizeof(sd->addele));
    memset(sd->addrace, 0, sizeof(sd->addrace));
    memset(sd->addsize, 0, sizeof(sd->addsize));
    memset(sd->addele_, 0, sizeof(sd->addele_));
    memset(sd->addrace_, 0, sizeof(sd->addrace_));
    memset(sd->addsize_, 0, sizeof(sd->addsize_));
    memset(sd->subele, 0, sizeof(sd->subele));
    memset(sd->subrace, 0, sizeof(sd->subrace));
    for (int& ire : sd->addeff)
        ire = 0;
    for (int& ire : sd->addeff2)
        ire = 0;
    for (int& ire : sd->reseff)
        ire = 0;
    memset(&sd->special_state, 0, sizeof(sd->special_state));
    memset(sd->weapon_coma_ele, 0, sizeof(sd->weapon_coma_ele));
    memset(sd->weapon_coma_race, 0, sizeof(sd->weapon_coma_race));

    sd->watk_ = 0;              //二刀流用(仮)
    sd->watk_2 = 0;
    sd->atk_ele_ = 0;
    sd->star_ = 0;
    sd->overrefine_ = 0;

    sd->aspd_rate = 100;
    sd->speed_rate = 100;
    sd->hprecov_rate = 100;
    sd->sprecov_rate = 100;
    sd->critical_def = 0;
    sd->double_rate = 0;
    sd->near_attack_def_rate = sd->long_attack_def_rate = 0;
    sd->atk_rate = sd->matk_rate = 100;
    sd->ignore_def_ele = sd->ignore_def_race = 0;
    sd->ignore_def_ele_ = sd->ignore_def_race_ = 0;
    sd->ignore_mdef_ele = sd->ignore_mdef_race = 0;
    sd->arrow_cri = 0;
    sd->magic_def_rate = sd->misc_def_rate = 0;
    memset(sd->arrow_addele, 0, sizeof(sd->arrow_addele));
    memset(sd->arrow_addrace, 0, sizeof(sd->arrow_addrace));
    memset(sd->arrow_addsize, 0, sizeof(sd->arrow_addsize));
    for (int& ire : sd->arrow_addeff)
        ire = 0;
    for (int& ire : sd->arrow_addeff2)
        ire = 0;
    memset(sd->magic_addele, 0, sizeof(sd->magic_addele));
    memset(sd->magic_addrace, 0, sizeof(sd->magic_addrace));
    memset(sd->magic_subrace, 0, sizeof(sd->magic_subrace));
    sd->perfect_hit = 0;
    sd->critical_rate = sd->hit_rate = sd->flee_rate = sd->flee2_rate = 100;
    sd->def_rate = sd->def2_rate = sd->mdef_rate = sd->mdef2_rate = 100;
    sd->def_ratio_atk_ele = sd->def_ratio_atk_ele_ = 0;
    sd->def_ratio_atk_race = sd->def_ratio_atk_race_ = 0;
    sd->get_zeny_num = 0;
    sd->add_damage_class_count = sd->add_damage_class_count_ =
        sd->add_magic_damage_class_count = 0;
    sd->add_def_class_count = sd->add_mdef_class_count = 0;
    sd->monster_drop_item_count = 0;
    memset(sd->add_damage_classrate, 0, sizeof(sd->add_damage_classrate));
    memset(sd->add_damage_classrate_, 0, sizeof(sd->add_damage_classrate_));
    memset(sd->add_magic_damage_classrate, 0,
            sizeof(sd->add_magic_damage_classrate));
    memset(sd->add_def_classrate, 0, sizeof(sd->add_def_classrate));
    memset(sd->add_mdef_classrate, 0, sizeof(sd->add_mdef_classrate));
    memset(sd->monster_drop_race, 0, sizeof(sd->monster_drop_race));
    memset(sd->monster_drop_itemrate, 0, sizeof(sd->monster_drop_itemrate));
    sd->speed_add_rate = sd->aspd_add_rate = 100;
    sd->double_add_rate = sd->perfect_hit_add = sd->get_zeny_add_num = 0;
    sd->splash_range = sd->splash_add_range = 0;
    sd->autospell_id = SkillID::ZERO;
    sd->autospell_lv = sd->autospell_rate = 0;
    sd->hp_drain_rate = sd->hp_drain_per = sd->sp_drain_rate =
        sd->sp_drain_per = 0;
    sd->hp_drain_rate_ = sd->hp_drain_per_ = sd->sp_drain_rate_ =
        sd->sp_drain_per_ = 0;
    sd->short_weapon_damage_return = sd->long_weapon_damage_return = 0;
    sd->magic_damage_return = 0;    //AppleGirl Was Here
    sd->random_attack_increase_add = sd->random_attack_increase_per = 0;

    sd->spellpower_bonus_target = 0;

    for (EQUIP i : EQUIPs_noarrow)
    {
        index = sd->equip_index[i];
        if (index < 0)
            continue;
        if (i == EQUIP::WEAPON && sd->equip_index[EQUIP::SHIELD] == index)
            continue;
        if (i == EQUIP::TORSO && sd->equip_index[EQUIP::LEGS] == index)
            continue;
        if (i == EQUIP::HAT
            && (sd->equip_index[EQUIP::TORSO] == index
                || sd->equip_index[EQUIP::LEGS] == index))
            continue;

        if (sd->inventory_data[index])
        {
            sd->spellpower_bonus_target +=
                sd->inventory_data[index]->magic_bonus;

            if (sd->inventory_data[index]->type == ItemType::WEAPON)
            {
                if (sd->status.inventory[index].card[0] != 0x00ff
                    && sd->status.inventory[index].card[0] != 0x00fe
                    && sd->status.inventory[index].card[0] != (short) 0xff00)
                {
                    int j;
                    for (j = 0; j < sd->inventory_data[index]->slot; j++)
                    {           // カード
                        int c = sd->status.inventory[index].card[j];
                        if (c > 0)
                        {
                            argrec_t arg[2];
                            arg[0].name = "@slotId";
                            arg[0].v.i = int(i);
                            arg[1].name = "@itemId";
                            arg[1].v.i = sd->inventory_data[index]->nameid;
                            if (i == EQUIP::SHIELD
                                && sd->status.inventory[index].equip == EPOS::SHIELD)
                                sd->state.lr_flag = 1;
                            run_script_l(itemdb_equipscript(c), 0, sd->bl.id,
                                        0, 2, arg);
                            sd->state.lr_flag = 0;
                        }
                    }
                }
            }
            else if (sd->inventory_data[index]->type == ItemType::ARMOR)
            {                   // 防具
                if (sd->status.inventory[index].card[0] != 0x00ff
                    && sd->status.inventory[index].card[0] != 0x00fe
                    && sd->status.inventory[index].card[0] != (short) 0xff00)
                {
                    int j;
                    for (j = 0; j < sd->inventory_data[index]->slot; j++)
                    {           // カード
                        int c = sd->status.inventory[index].card[j];
                        if (c > 0) {
                            argrec_t arg[2];
                            arg[0].name = "@slotId";
                            arg[0].v.i = int(i);
                            arg[1].name = "@itemId";
                            arg[1].v.i = sd->inventory_data[index]->nameid;
                            run_script_l(itemdb_equipscript(c), 0, sd->bl.id,
                                        0, 2, arg);
                        }
                    }
                }
            }
        }
    }

#ifdef USE_ASTRAL_SOUL_SKILL
    if (sd->spellpower_bonus_target < 0)
        sd->spellpower_bonus_target =
            (sd->spellpower_bonus_target * 256) /
            (min(128 + skill_power(sd, TMW_ASTRAL_SOUL), 256));
#endif

    if (sd->spellpower_bonus_target < sd->spellpower_bonus_current)
        sd->spellpower_bonus_current = sd->spellpower_bonus_target;

    wele = sd->atk_ele;
    wele_ = sd->atk_ele_;
    def_ele = sd->def_ele;
    sd->paramcard = sd->parame;

    // 装備品によるステータス変化はここで実行
    for (EQUIP i : EQUIPs_noarrow)
    {
        index = sd->equip_index[i];
        if (index < 0)
            continue;
        if (i == EQUIP::WEAPON && sd->equip_index[EQUIP::SHIELD] == index)
            continue;
        if (i == EQUIP::TORSO && sd->equip_index[EQUIP::LEGS] == index)
            continue;
        if (i == EQUIP::HAT
            && (sd->equip_index[EQUIP::TORSO] == index
                || sd->equip_index[EQUIP::LEGS] == index))
            continue;
        if (sd->inventory_data[index])
        {
            sd->def += sd->inventory_data[index]->def;
            if (sd->inventory_data[index]->type == ItemType::WEAPON)
            {
                int r;
                if (i == EQUIP::SHIELD
                    && sd->status.inventory[index].equip == EPOS::SHIELD)
                {
                    //二刀流用データ入力
                    sd->watk_ += sd->inventory_data[index]->atk;
                    sd->watk_2 = (r = sd->status.inventory[index].refine) * // 精錬攻撃力
                        0;
                    if ((r -= 10) > 0) // 過剰精錬ボーナス
                        sd->overrefine_ = r * 0;

                    if (sd->status.inventory[index].card[0] == 0x00ff)
                    {           // 製造武器
                        sd->star_ = (sd->status.inventory[index].card[1] >> 8); // 星のかけら
                        wele_ = (sd->status.inventory[index].card[1] & 0x0f);   // 属 性
                    }
                    sd->attackrange_ += sd->inventory_data[index]->range;
                    sd->state.lr_flag = 1;
                    {
                        argrec_t arg[2];
                        arg[0].name = "@slotId";
                        arg[0].v.i = int(i);
                        arg[1].name = "@itemId";
                        arg[1].v.i = sd->inventory_data[index]->nameid;
                        run_script_l(sd->inventory_data[index]->equip_script, 0,
                                      sd->bl.id, 0, 2, arg);
                    }
                    sd->state.lr_flag = 0;
                }
                else
                {               //二刀流武器以外
                    argrec_t arg[2];
                    arg[0].name = "@slotId";
                    arg[0].v.i = int(i);
                    arg[1].name = "@itemId";
                    arg[1].v.i = sd->inventory_data[index]->nameid;
                    sd->watk += sd->inventory_data[index]->atk;
                    sd->watk2 += (r = sd->status.inventory[index].refine) * // 精錬攻撃力
                        0;
                    if ((r -= 10) > 0) // 過剰精錬ボーナス
                        sd->overrefine += r * 0;

                    if (sd->status.inventory[index].card[0] == 0x00ff)
                    {           // 製造武器
                        sd->star += (sd->status.inventory[index].card[1] >> 8); // 星のかけら
                        wele = (sd->status.inventory[index].card[1] & 0x0f);    // 属 性
                    }
                    sd->attackrange += sd->inventory_data[index]->range;
                    run_script_l(sd->inventory_data[index]->equip_script, 0,
                                  sd->bl.id, 0, 2, arg);
                }
            }
            else if (sd->inventory_data[index]->type == ItemType::ARMOR)
            {
                argrec_t arg[2];
                arg[0].name = "@slotId";
                arg[0].v.i = int(i);
                arg[1].name = "@itemId";
                arg[1].v.i = sd->inventory_data[index]->nameid;
                sd->watk += sd->inventory_data[index]->atk;
                refinedef +=
                    sd->status.inventory[index].refine * 0;
                run_script_l(sd->inventory_data[index]->equip_script, 0,
                              sd->bl.id, 0, 2, arg);
            }
        }
    }

    if (battle_is_unarmed(&sd->bl))
    {
        sd->watk += skill_power(sd, TMW_BRAWLING) / 3; // +66 for 200
        sd->watk2 += skill_power(sd, TMW_BRAWLING) >> 3;   // +25 for 200
        sd->watk_ += skill_power(sd, TMW_BRAWLING) / 3;    // +66 for 200
        sd->watk_2 += skill_power(sd, TMW_BRAWLING) >> 3;  // +25 for 200
    }

    if (sd->equip_index[EQUIP::ARROW] >= 0)
    {                           // 矢
        index = sd->equip_index[EQUIP::ARROW];
        if (sd->inventory_data[index])
        {                       //まだ属性が入っていない
            argrec_t arg[2];
            arg[0].name = "@slotId";
            arg[0].v.i = int(EQUIP::ARROW);
            arg[1].name = "@itemId";
            arg[1].v.i = sd->inventory_data[index]->nameid;
            sd->state.lr_flag = 2;
            run_script_l(sd->inventory_data[index]->equip_script, 0, sd->bl.id,
                        0, 2, arg);
            sd->state.lr_flag = 0;
            sd->arrow_atk += sd->inventory_data[index]->atk;
        }
    }
    sd->def += (refinedef + 50) / 100;

    if (sd->attackrange < 1)
        sd->attackrange = 1;
    if (sd->attackrange_ < 1)
        sd->attackrange_ = 1;
    if (sd->attackrange < sd->attackrange_)
        sd->attackrange = sd->attackrange_;
    if (sd->status.weapon == 11)
        sd->attackrange += sd->arrow_range;
    if (wele > 0)
        sd->atk_ele = wele;
    if (wele_ > 0)
        sd->atk_ele_ = wele_;
    if (def_ele > 0)
        sd->def_ele = def_ele;
    sd->double_rate += sd->double_add_rate;
    sd->perfect_hit += sd->perfect_hit_add;
    sd->get_zeny_num += sd->get_zeny_add_num;
    sd->splash_range += sd->splash_add_range;
    if (sd->speed_add_rate != 100)
        sd->speed_rate += sd->speed_add_rate - 100;
    if (sd->aspd_add_rate != 100)
        sd->aspd_rate += sd->aspd_add_rate - 100;

    // ステータス変化による基本パラメータ補正
    if (sd->sc_count)
    {
        if (sd->sc_data[SC_CONCENTRATE].timer != -1
            && sd->sc_data[SC_QUAGMIRE].timer == -1)
        {                       // 集中力向上
            sd->paramb[ATTR::AGI] +=
                (sd->status.attrs[ATTR::AGI] + sd->paramb[ATTR::AGI] + sd->parame[ATTR::AGI] -
                 sd->paramcard[ATTR::AGI]) * (2 +
                                      sd->sc_data[SC_CONCENTRATE].val1) / 100;
            sd->paramb[ATTR::DEX] +=
                (sd->status.attrs[ATTR::DEX] + sd->paramb[ATTR::DEX] + sd->parame[ATTR::DEX] -
                 sd->paramcard[ATTR::DEX]) * (2 +
                                      sd->sc_data[SC_CONCENTRATE].val1) / 100;
        }
        if (sd->sc_data[SC_INCREASEAGI].timer != -1
            && sd->sc_data[SC_QUAGMIRE].timer == -1
            && sd->sc_data[SC_DONTFORGETME].timer == -1)
        {                       // 速度増加
            sd->paramb[ATTR::AGI] += 2 + sd->sc_data[SC_INCREASEAGI].val1;
            sd->speed -= sd->speed * 25 / 100;
        }
        if (sd->sc_data[SC_DECREASEAGI].timer != -1)    // 速度減少(agiはbattle.cで)
            sd->speed = sd->speed * 125 / 100;
        if (sd->sc_data[SC_CLOAKING].timer != -1)
            sd->speed =
                (sd->speed * (76 + (sd->sc_data[SC_INCREASEAGI].val1 * 3))) /
                100;
        if (sd->sc_data[SC_BLESSING].timer != -1)
        {                       // ブレッシング
            sd->paramb[ATTR::STR] += sd->sc_data[SC_BLESSING].val1;
            sd->paramb[ATTR::INT] += sd->sc_data[SC_BLESSING].val1;
            sd->paramb[ATTR::DEX] += sd->sc_data[SC_BLESSING].val1;
        }
        if (sd->sc_data[SC_GLORIA].timer != -1) // グロリア
            sd->paramb[ATTR::LUK] += 30;
        if (sd->sc_data[SC_LOUD].timer != -1 && sd->sc_data[SC_QUAGMIRE].timer == -1)   // ラウドボイス
            sd->paramb[ATTR::STR] += 4;
        if (sd->sc_data[SC_QUAGMIRE].timer != -1)
        {                       // クァグマイア
            sd->speed = sd->speed * 3 / 2;
            sd->paramb[ATTR::AGI] -=
                (sd->status.attrs[ATTR::AGI] + sd->paramb[ATTR::AGI] + sd->parame[ATTR::AGI]) / 2;
            sd->paramb[ATTR::DEX] -=
                (sd->status.attrs[ATTR::DEX] + sd->paramb[ATTR::DEX] + sd->parame[ATTR::DEX]) / 2;
        }
        if (sd->sc_data[SC_TRUESIGHT].timer != -1)
        {                       // トゥルーサイト
            for (auto& p : sd->paramb)
                p += 5;
        }
    }
    sd->speed -= skill_power(sd, TMW_SPEED) >> 3;
    sd->aspd_rate -= skill_power(sd, TMW_SPEED) / 10;
    if (sd->aspd_rate < 20)
        sd->aspd_rate = 20;

    for (ATTR attr : ATTRs)
        sd->paramc[attr] = max(0, sd->status.attrs[attr] + sd->paramb[attr] + sd->parame[attr]);

    if (sd->status.weapon == 11 || sd->status.weapon == 13
        || sd->status.weapon == 14)
    {
        str = sd->paramc[ATTR::DEX];
        dex = sd->paramc[ATTR::STR];
    }
    else
    {
        str = sd->paramc[ATTR::STR];
        dex = sd->paramc[ATTR::DEX];
        sd->critical += ((dex * 3) >> 1);
    }
    dstr = str / 10;
    sd->base_atk += str + dstr * dstr + dex / 5 + sd->paramc[ATTR::LUK] / 5;
//FPRINTF(stderr, "baseatk = %d = x + %d + %d + %d + %d\n", sd->base_atk, str, dstr*dstr, dex/5, sd->paramc[ATTR::LUK]/5);
    sd->matk1 += sd->paramc[ATTR::INT] + (sd->paramc[ATTR::INT] / 5) * (sd->paramc[ATTR::INT] / 5);
    sd->matk2 += sd->paramc[ATTR::INT] + (sd->paramc[ATTR::INT] / 7) * (sd->paramc[ATTR::INT] / 7);
    if (sd->matk1 < sd->matk2)
    {
        int temp = sd->matk2;
        sd->matk2 = sd->matk1;
        sd->matk1 = temp;
    }
    // [Fate] New tmw magic system
    sd->matk1 += sd->status.base_level + sd->spellpower_bonus_current;
#ifdef USE_ASTRAL_SOUL_SKILL
    if (sd->matk1 > MAGIC_SKILL_THRESHOLD)
    {
        int bonus = sd->matk1 - MAGIC_SKILL_THRESHOLD;
        // Ok if you are above a certain threshold, you get only (1/8) of that matk1
        // if you have Astral soul skill you can get the whole power again (and additionally the 1/8 added)
        sd->matk1 = MAGIC_SKILL_THRESHOLD + (bonus>>3) + ((3*bonus*skill_power(sd, TMW_ASTRAL_SOUL))>>9);
    }
#endif
    sd->matk2 = 0;
    if (sd->matk1 < 0)
        sd->matk1 = 0;

    sd->hit += sd->paramc[ATTR::DEX] + sd->status.base_level;
    sd->flee += sd->paramc[ATTR::AGI] + sd->status.base_level;
    sd->def2 += sd->paramc[ATTR::VIT];
    sd->mdef2 += sd->paramc[ATTR::INT];
    sd->flee2 += sd->paramc[ATTR::LUK] + 10;
    sd->critical += (sd->paramc[ATTR::LUK] * 3) + 10;

    // 200 is the maximum of the skill
    // def2 is the defence gained by vit, whereas "def", which is gained by armor, stays as is
    int spbsk = skill_power(sd, TMW_RAGING);
    if (spbsk != 0 && sd->attackrange <= 2)
    {
        sd->critical += sd->critical * spbsk / 100;
        sd->def2 = (sd->def2 * 256) / (256 + spbsk);
    }

    if (sd->base_atk < 1)
        sd->base_atk = 1;
    if (sd->critical_rate != 100)
        sd->critical = (sd->critical * sd->critical_rate) / 100;
    if (sd->critical < 10)
        sd->critical = 10;
    if (sd->hit_rate != 100)
        sd->hit = (sd->hit * sd->hit_rate) / 100;
    if (sd->hit < 1)
        sd->hit = 1;
    if (sd->flee_rate != 100)
        sd->flee = (sd->flee * sd->flee_rate) / 100;
    if (sd->flee < 1)
        sd->flee = 1;
    if (sd->flee2_rate != 100)
        sd->flee2 = (sd->flee2 * sd->flee2_rate) / 100;
    if (sd->flee2 < 10)
        sd->flee2 = 10;
    if (sd->def_rate != 100)
        sd->def = (sd->def * sd->def_rate) / 100;
    if (sd->def < 0)
        sd->def = 0;
    if (sd->def2_rate != 100)
        sd->def2 = (sd->def2 * sd->def2_rate) / 100;
    if (sd->def2 < 1)
        sd->def2 = 1;
    if (sd->mdef_rate != 100)
        sd->mdef = (sd->mdef * sd->mdef_rate) / 100;
    if (sd->mdef < 0)
        sd->mdef = 0;
    if (sd->mdef2_rate != 100)
        sd->mdef2 = (sd->mdef2 * sd->mdef2_rate) / 100;
    if (sd->mdef2 < 1)
        sd->mdef2 = 1;

    // 二刀流 ASPD 修正
    if (sd->status.weapon <= 16)
        sd->aspd += aspd_base_0[sd->status.weapon]
            - (sd->paramc[ATTR::AGI] * 4 + sd->paramc[ATTR::DEX])
            * aspd_base_0[sd->status.weapon] / 1000;
    else
        sd->aspd += (
                (aspd_base_0[sd->weapontype1]
                    - (sd->paramc[ATTR::AGI] * 4 + sd->paramc[ATTR::DEX])
                    * aspd_base_0[sd->weapontype1] / 1000)
                + (aspd_base_0[sd->weapontype2]
                    - (sd->paramc[ATTR::AGI] * 4 + sd->paramc[ATTR::DEX])
                    * aspd_base_0[sd->weapontype2] / 1000)
                )
            * 140 / 200;

    aspd_rate = sd->aspd_rate;

    //攻撃速度増加

    if (sd->attackrange > 2)
    {
        // [fate] ranged weapon?
        sd->attackrange += min(skill_power(sd, AC_OWL) / 60, 3);
        sd->hit += skill_power(sd, AC_OWL) / 10;   // 20 for 200
    }

    sd->max_weight += 1000;
    if (sd->sc_count)
    {
        if (sd->sc_data[SC_WINDWALK].timer != -1)   //ウィンドウォーク時はLv*2%減算
            sd->speed -=
                sd->speed * (sd->sc_data[SC_WINDWALK].val1 * 2) / 100;
        if (sd->sc_data[SC_CARTBOOST].timer != -1)  // カートブースト
            sd->speed -= (DEFAULT_WALK_SPEED * 20) / 100;
        if (sd->sc_data[SC_BERSERK].timer != -1)    //バーサーク中はIAと同じぐらい速い？
            sd->speed -= sd->speed * 25 / 100;
        if (sd->sc_data[SC_WEDDING].timer != -1)    //結婚中は歩くのが遅い
            sd->speed = 2 * DEFAULT_WALK_SPEED;
    }

    bl = sd->status.base_level;

    sd->status.max_hp += (
            3500
            + bl * hp_coefficient2_0
            + hp_sigma_val_0[(bl > 0) ? bl - 1 : 0]
            ) / 100 * (100 + sd->paramc[ATTR::VIT]) / 100
        + (sd->parame[ATTR::VIT] - sd->paramcard[ATTR::VIT]);
    if (sd->hprate != 100)
        sd->status.max_hp = sd->status.max_hp * sd->hprate / 100;

    if (sd->sc_data[SC_BERSERK].timer != -1)
    {                           // バーサーク
        sd->status.max_hp = sd->status.max_hp * 3;
        sd->status.hp = sd->status.hp * 3;
        if (sd->status.max_hp > battle_config.max_hp)   // removed negative max hp bug by Valaris
            sd->status.max_hp = battle_config.max_hp;
        if (sd->status.hp > battle_config.max_hp)   // removed negative max hp bug by Valaris
            sd->status.hp = battle_config.max_hp;
    }

    if (sd->status.max_hp > battle_config.max_hp)   // removed negative max hp bug by Valaris
        sd->status.max_hp = battle_config.max_hp;
    if (sd->status.max_hp <= 0)
        sd->status.max_hp = 1;  // end

    // 最大SP計算
    sd->status.max_sp += ((sp_coefficient_0 * bl) + 1000)
        / 100 * (100 + sd->paramc[ATTR::INT]) / 100
        + (sd->parame[ATTR::INT] - sd->paramcard[ATTR::INT]);
    if (sd->sprate != 100)
        sd->status.max_sp = sd->status.max_sp * sd->sprate / 100;

    if (sd->status.max_sp < 0 || sd->status.max_sp > battle_config.max_sp)
        sd->status.max_sp = battle_config.max_sp;

    //自然回復HP
    sd->nhealhp = 1 + (sd->paramc[ATTR::VIT] / 5) + (sd->status.max_hp / 200);
    //自然回復SP
    sd->nhealsp = 1 + (sd->paramc[ATTR::INT] / 6) + (sd->status.max_sp / 100);
    if (sd->paramc[ATTR::INT] >= 120)
        sd->nhealsp += ((sd->paramc[ATTR::INT] - 120) >> 1) + 4;

    if (sd->hprecov_rate != 100)
    {
        sd->nhealhp = sd->nhealhp * sd->hprecov_rate / 100;
        if (sd->nhealhp < 1)
            sd->nhealhp = 1;
    }
    if (sd->sprecov_rate != 100)
    {
        sd->nhealsp = sd->nhealsp * sd->sprecov_rate / 100;
        if (sd->nhealsp < 1)
            sd->nhealsp = 1;
    }

    // スキルやステータス異常による残りのパラメータ補正
    if (sd->sc_count)
    {
        // ATK/DEF変化形
        if (sd->sc_data[SC_ANGELUS].timer != -1)    // エンジェラス
            sd->def2 =
                sd->def2 * (110 + 5 * sd->sc_data[SC_ANGELUS].val1) / 100;
        if (sd->sc_data[SC_IMPOSITIO].timer != -1)
        {                       // インポシティオマヌス
            sd->watk += sd->sc_data[SC_IMPOSITIO].val1 * 5;
            index = sd->equip_index[EQUIP::SHIELD];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == ItemType::WEAPON)
                sd->watk_ += sd->sc_data[SC_IMPOSITIO].val1 * 5;
        }
        if (sd->sc_data[SC_PROVOKE].timer != -1)
        {                       // プロボック
            sd->def2 =
                sd->def2 * (100 - 6 * sd->sc_data[SC_PROVOKE].val1) / 100;
            sd->base_atk =
                sd->base_atk * (100 + 2 * sd->sc_data[SC_PROVOKE].val1) / 100;
            sd->watk =
                sd->watk * (100 + 2 * sd->sc_data[SC_PROVOKE].val1) / 100;
            index = sd->equip_index[EQUIP::SHIELD];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == ItemType::WEAPON)
                sd->watk_ =
                    sd->watk_ * (100 +
                                 2 * sd->sc_data[SC_PROVOKE].val1) / 100;
        }
        if (sd->sc_data[SC_ENDURE].timer != -1)
            sd->mdef2 += sd->sc_data[SC_ENDURE].val1;
        if (sd->sc_data[SC_MINDBREAKER].timer != -1)
        {                       // プロボック
            sd->mdef2 =
                sd->mdef2 * (100 -
                             6 * sd->sc_data[SC_MINDBREAKER].val1) / 100;
            sd->matk1 =
                sd->matk1 * (100 +
                             2 * sd->sc_data[SC_MINDBREAKER].val1) / 100;
            sd->matk2 =
                sd->matk2 * (100 +
                             2 * sd->sc_data[SC_MINDBREAKER].val1) / 100;
        }
        if (sd->sc_data[SC_POISON].timer != -1) // 毒状態
            sd->def2 = sd->def2 * 75 / 100;
        if (sd->sc_data[SC_DRUMBATTLE].timer != -1)
        {                       // 戦太鼓の響き
            sd->watk += sd->sc_data[SC_DRUMBATTLE].val2;
            sd->def += sd->sc_data[SC_DRUMBATTLE].val3;
            index = sd->equip_index[EQUIP::SHIELD];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == ItemType::WEAPON)
                sd->watk_ += sd->sc_data[SC_DRUMBATTLE].val2;
        }
        if (sd->sc_data[SC_NIBELUNGEN].timer != -1)
        {                       // ニーベルングの指輪
            index = sd->equip_index[EQUIP::WEAPON];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 3)
                sd->watk += sd->sc_data[SC_NIBELUNGEN].val3;
            index = sd->equip_index[EQUIP::SHIELD];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 3)
                sd->watk_ += sd->sc_data[SC_NIBELUNGEN].val3;
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 4)
                sd->watk += sd->sc_data[SC_NIBELUNGEN].val2;
            index = sd->equip_index[EQUIP::SHIELD];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->wlv == 4)
                sd->watk_ += sd->sc_data[SC_NIBELUNGEN].val2;
        }

        if (sd->sc_data[SC_VOLCANO].timer != -1 && sd->def_ele == 3)
        {                       // ボルケーノ
            sd->watk += sd->sc_data[SC_VIOLENTGALE].val3;
        }

        if (sd->sc_data[SC_SIGNUMCRUCIS].timer != -1)
            sd->def =
                sd->def * (100 - sd->sc_data[SC_SIGNUMCRUCIS].val2) / 100;
        if (sd->sc_data[SC_ETERNALCHAOS].timer != -1)   // エターナルカオス
            sd->def = 0;

        if (sd->sc_data[SC_CONCENTRATION].timer != -1)
        {                       //コンセントレーション
            sd->watk =
                sd->watk * (100 +
                            5 * sd->sc_data[SC_CONCENTRATION].val1) / 100;
            index = sd->equip_index[EQUIP::SHIELD];
            if (index >= 0 && sd->inventory_data[index]
                && sd->inventory_data[index]->type == ItemType::WEAPON)
                sd->watk_ =
                    sd->watk * (100 +
                                5 * sd->sc_data[SC_CONCENTRATION].val1) / 100;
            sd->def =
                sd->def * (100 -
                           5 * sd->sc_data[SC_CONCENTRATION].val1) / 100;
        }

        if (sd->sc_data[SC_MAGICPOWER].timer != -1)
        {                       //魔法力増幅
            sd->matk1 =
                sd->matk1 * (100 + 2 * sd->sc_data[SC_MAGICPOWER].val1) / 100;
            sd->matk2 =
                sd->matk2 * (100 + 2 * sd->sc_data[SC_MAGICPOWER].val1) / 100;
        }
        if (sd->sc_data[SC_ATKPOT].timer != -1)
            sd->watk += sd->sc_data[SC_ATKPOT].val1;
        if (sd->sc_data[SC_MATKPOT].timer != -1)
        {
            sd->matk1 += sd->sc_data[SC_MATKPOT].val1;
            sd->matk2 += sd->sc_data[SC_MATKPOT].val1;
        }

        // ASPD/移動速度変化系
        if (sd->sc_data[SC_TWOHANDQUICKEN].timer != -1 && sd->sc_data[SC_QUAGMIRE].timer == -1 && sd->sc_data[SC_DONTFORGETME].timer == -1) // 2HQ
            aspd_rate -= 30;
        if (sd->sc_data[SC_ADRENALINE].timer != -1
            && sd->sc_data[SC_TWOHANDQUICKEN].timer == -1
            && sd->sc_data[SC_QUAGMIRE].timer == -1
            && sd->sc_data[SC_DONTFORGETME].timer == -1)
        {                       // アドレナリンラッシュ
            if (sd->sc_data[SC_ADRENALINE].val2
                || !battle_config.party_skill_penaly)
                aspd_rate -= 30;
            else
                aspd_rate -= 25;
        }
        if (sd->sc_data[SC_SPEARSQUICKEN].timer != -1 && sd->sc_data[SC_ADRENALINE].timer == -1 && sd->sc_data[SC_TWOHANDQUICKEN].timer == -1 && sd->sc_data[SC_QUAGMIRE].timer == -1 && sd->sc_data[SC_DONTFORGETME].timer == -1)  // スピアクィッケン
            aspd_rate -= sd->sc_data[SC_SPEARSQUICKEN].val2;
        if (sd->sc_data[SC_ASSNCROS].timer != -1 && // 夕陽のアサシンクロス
            sd->sc_data[SC_TWOHANDQUICKEN].timer == -1
            && sd->sc_data[SC_ADRENALINE].timer == -1
            && sd->sc_data[SC_SPEARSQUICKEN].timer == -1
            && sd->sc_data[SC_DONTFORGETME].timer == -1)
            aspd_rate -=
                5 + sd->sc_data[SC_ASSNCROS].val1 +
                sd->sc_data[SC_ASSNCROS].val2 + sd->sc_data[SC_ASSNCROS].val3;
        if (sd->sc_data[SC_DONTFORGETME].timer != -1)
        {                       // 私を忘れないで
            aspd_rate +=
                sd->sc_data[SC_DONTFORGETME].val1 * 3 +
                sd->sc_data[SC_DONTFORGETME].val2 +
                (sd->sc_data[SC_DONTFORGETME].val3 >> 16);
            sd->speed =
                sd->speed * (100 + sd->sc_data[SC_DONTFORGETME].val1 * 2 +
                             sd->sc_data[SC_DONTFORGETME].val2 +
                             (sd->sc_data[SC_DONTFORGETME].val3 & 0xffff)) /
                100;
        }
        {
            StatusChange i;
            if (sd->sc_data[i = SC_SPEEDPOTION2].timer != -1
                || sd->sc_data[i = SC_SPEEDPOTION1].timer != -1
                || sd->sc_data[i = SC_SPEEDPOTION0].timer != -1)
                aspd_rate -= sd->sc_data[i].val1;
        }

        if (sd->sc_data[SC_HASTE].timer != -1)
            aspd_rate -= sd->sc_data[SC_HASTE].val1;

        /* Slow down if protected */

        if (sd->sc_data[SC_PHYS_SHIELD].timer != -1)
            aspd_rate += sd->sc_data[SC_PHYS_SHIELD].val1;

        // HIT/FLEE変化系
        if (sd->sc_data[SC_WHISTLE].timer != -1)
        {                       // 口笛
            sd->flee += sd->flee * (sd->sc_data[SC_WHISTLE].val1
                                    + sd->sc_data[SC_WHISTLE].val2 +
                                    (sd->sc_data[SC_WHISTLE].val3 >> 16)) /
                100;
            sd->flee2 +=
                (sd->sc_data[SC_WHISTLE].val1 + sd->sc_data[SC_WHISTLE].val2 +
                 (sd->sc_data[SC_WHISTLE].val3 & 0xffff)) * 10;
        }
        if (sd->sc_data[SC_HUMMING].timer != -1)    // ハミング
            sd->hit +=
                (sd->sc_data[SC_HUMMING].val1 * 2 +
                 sd->sc_data[SC_HUMMING].val2 +
                 sd->sc_data[SC_HUMMING].val3) * sd->hit / 100;
        if (sd->sc_data[SC_VIOLENTGALE].timer != -1 && sd->def_ele == 4)
        {                       // バイオレントゲイル
            sd->flee += sd->flee * sd->sc_data[SC_VIOLENTGALE].val3 / 100;
        }
        if (sd->sc_data[SC_BLIND].timer != -1)
        {                       // 暗黒
            sd->hit -= sd->hit * 25 / 100;
            sd->flee -= sd->flee * 25 / 100;
        }
        if (sd->sc_data[SC_WINDWALK].timer != -1)   // ウィンドウォーク
            sd->flee += sd->flee * (sd->sc_data[SC_WINDWALK].val2) / 100;
        if (sd->sc_data[SC_SPIDERWEB].timer != -1)  //スパイダーウェブ
            sd->flee -= sd->flee * 50 / 100;
        if (sd->sc_data[SC_TRUESIGHT].timer != -1)  //トゥルーサイト
            sd->hit += 3 * (sd->sc_data[SC_TRUESIGHT].val1);
        if (sd->sc_data[SC_CONCENTRATION].timer != -1)  //コンセントレーション
            sd->hit += (10 * (sd->sc_data[SC_CONCENTRATION].val1));

        // 耐性
        if (sd->sc_data[SC_SIEGFRIED].timer != -1)
        {                       // 不死身のジークフリード
            sd->subele[1] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[2] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[3] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[4] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[5] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[6] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[7] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[8] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
            sd->subele[9] += sd->sc_data[SC_SIEGFRIED].val2;    // 水
        }
        if (sd->sc_data[SC_PROVIDENCE].timer != -1)
        {                       // プロヴィデンス
            sd->subele[6] += sd->sc_data[SC_PROVIDENCE].val2;   // 対 聖属性
            sd->subrace[6] += sd->sc_data[SC_PROVIDENCE].val2;  // 対 悪魔
        }

        // その他
        if (sd->sc_data[SC_APPLEIDUN].timer != -1)
        {                       // イドゥンの林檎
            sd->status.max_hp +=
                ((5 + sd->sc_data[SC_APPLEIDUN].val1 * 2 +
                  ((sd->sc_data[SC_APPLEIDUN].val2 + 1) >> 1) +
                  sd->sc_data[SC_APPLEIDUN].val3 / 10) * sd->status.max_hp) /
                100;
            if (sd->status.max_hp < 0
                || sd->status.max_hp > battle_config.max_hp)
                sd->status.max_hp = battle_config.max_hp;
        }
        if (sd->sc_data[SC_DELUGE].timer != -1 && sd->def_ele == 1)
        {                       // デリュージ
            sd->status.max_hp +=
                sd->status.max_hp * sd->sc_data[SC_DELUGE].val3 / 100;
            if (sd->status.max_hp < 0
                || sd->status.max_hp > battle_config.max_hp)
                sd->status.max_hp = battle_config.max_hp;
        }
        if (sd->sc_data[SC_SERVICE4U].timer != -1)
        {                       // サービスフォーユー
            sd->status.max_sp +=
                sd->status.max_sp * (10 + sd->sc_data[SC_SERVICE4U].val1 +
                                     sd->sc_data[SC_SERVICE4U].val2 +
                                     sd->sc_data[SC_SERVICE4U].val3) / 100;
            if (sd->status.max_sp < 0
                || sd->status.max_sp > battle_config.max_sp)
                sd->status.max_sp = battle_config.max_sp;
            sd->dsprate -=
                (10 + sd->sc_data[SC_SERVICE4U].val1 * 3 +
                 sd->sc_data[SC_SERVICE4U].val2 +
                 sd->sc_data[SC_SERVICE4U].val3);
            if (sd->dsprate < 0)
                sd->dsprate = 0;
        }

        if (sd->sc_data[SC_FORTUNE].timer != -1)    // 幸運のキス
            sd->critical +=
                (10 + sd->sc_data[SC_FORTUNE].val1 +
                 sd->sc_data[SC_FORTUNE].val2 +
                 sd->sc_data[SC_FORTUNE].val3) * 10;

        if (sd->sc_data[SC_EXPLOSIONSPIRITS].timer != -1)
        {                       // 爆裂波動
            sd->critical += sd->sc_data[SC_EXPLOSIONSPIRITS].val2;
        }

        if (sd->sc_data[SC_STEELBODY].timer != -1)
        {                       // 金剛
            sd->def = 90;
            sd->mdef = 90;
            aspd_rate += 25;
            sd->speed = (sd->speed * 125) / 100;
        }
        if (sd->sc_data[SC_DEFENDER].timer != -1)
        {
            sd->aspd += (550 - sd->sc_data[SC_DEFENDER].val1 * 50);
            sd->speed =
                (sd->speed * (155 - sd->sc_data[SC_DEFENDER].val1 * 5)) / 100;
        }
        if (sd->sc_data[SC_ENCPOISON].timer != -1)
            sd->addeff[BadSC::POISON] += sd->sc_data[SC_ENCPOISON].val2;

        if (sd->sc_data[SC_DANCING].timer != -1)
        {                       // 演奏/ダンス使用中
            sd->speed *= 4;
            sd->nhealsp = 0;
            sd->nshealsp = 0;
            sd->nsshealsp = 0;
        }
        if (sd->sc_data[SC_CURSE].timer != -1)
            sd->speed += 450;

        if (sd->sc_data[SC_TRUESIGHT].timer != -1)  //トゥルーサイト
            sd->critical +=
                sd->critical * (sd->sc_data[SC_TRUESIGHT].val1) / 100;

/*              if (sd->sc_data[SC_VOLCANO].timer!=-1)  // エンチャントポイズン(属性はbattle.cで)
                        sd->addeff[2]+=sd->sc_data[SC_VOLCANO].val2;//% of granting
                if (sd->sc_data[SC_DELUGE].timer!=-1)   // エンチャントポイズン(属性はbattle.cで)
                        sd->addeff[0]+=sd->sc_data[SC_DELUGE].val2;//% of granting
                */
    }

    if (sd->speed_rate != 100)
        sd->speed = sd->speed * sd->speed_rate / 100;
    if (sd->speed < 1)
        sd->speed = 1;
    if (aspd_rate != 100)
        sd->aspd = sd->aspd * aspd_rate / 100;

    if (sd->attack_spell_override)
        sd->aspd = sd->attack_spell_delay;

    if (sd->aspd < battle_config.max_aspd)
        sd->aspd = battle_config.max_aspd;
    sd->amotion = sd->aspd;
    sd->dmotion = 800 - sd->paramc[ATTR::AGI] * 4;
    if (sd->dmotion < 400)
        sd->dmotion = 400;

    if (sd->status.hp > sd->status.max_hp)
        sd->status.hp = sd->status.max_hp;
    if (sd->status.sp > sd->status.max_sp)
        sd->status.sp = sd->status.max_sp;

    if (first & 4)
        return 0;
    if (first & 3)
    {
        clif_updatestatus(sd, SP_SPEED);
        clif_updatestatus(sd, SP_MAXHP);
        clif_updatestatus(sd, SP_MAXSP);
        if (first & 1)
        {
            clif_updatestatus(sd, SP_HP);
            clif_updatestatus(sd, SP_SP);
        }
        return 0;
    }

    if (memcmp(&b_skill, &sd->status.skill, sizeof(sd->status.skill))
        || b_attackrange != sd->attackrange)
        clif_skillinfoblock(sd);   // スキル送信

    if (b_speed != sd->speed)
        clif_updatestatus(sd, SP_SPEED);
    if (b_weight != sd->weight)
        clif_updatestatus(sd, SP_WEIGHT);
    if (b_max_weight != sd->max_weight)
    {
        clif_updatestatus(sd, SP_MAXWEIGHT);
        pc_checkweighticon(sd);
    }
    for (ATTR i : ATTRs)
        if (b_paramb[i] + b_parame[i] != sd->paramb[i] + sd->parame[i])
            clif_updatestatus(sd, attr_to_sp(i));
    if (b_hit != sd->hit)
        clif_updatestatus(sd, SP_HIT);
    if (b_flee != sd->flee)
        clif_updatestatus(sd, SP_FLEE1);
    if (b_aspd != sd->aspd)
        clif_updatestatus(sd, SP_ASPD);
    if (b_watk != sd->watk || b_base_atk != sd->base_atk)
        clif_updatestatus(sd, SP_ATK1);
    if (b_def != sd->def)
        clif_updatestatus(sd, SP_DEF1);
    if (b_watk2 != sd->watk2)
        clif_updatestatus(sd, SP_ATK2);
    if (b_def2 != sd->def2)
        clif_updatestatus(sd, SP_DEF2);
    if (b_flee2 != sd->flee2)
        clif_updatestatus(sd, SP_FLEE2);
    if (b_critical != sd->critical)
        clif_updatestatus(sd, SP_CRITICAL);
    if (b_matk1 != sd->matk1)
        clif_updatestatus(sd, SP_MATK1);
    if (b_matk2 != sd->matk2)
        clif_updatestatus(sd, SP_MATK2);
    if (b_mdef != sd->mdef)
        clif_updatestatus(sd, SP_MDEF1);
    if (b_mdef2 != sd->mdef2)
        clif_updatestatus(sd, SP_MDEF2);
    if (b_attackrange != sd->attackrange)
        clif_updatestatus(sd, SP_ATTACKRANGE);
    if (b_max_hp != sd->status.max_hp)
        clif_updatestatus(sd, SP_MAXHP);
    if (b_max_sp != sd->status.max_sp)
        clif_updatestatus(sd, SP_MAXSP);
    if (b_hp != sd->status.hp)
        clif_updatestatus(sd, SP_HP);
    if (b_sp != sd->status.sp)
        clif_updatestatus(sd, SP_SP);

    return 0;
}

/*==========================================
 * 装 備品による能力等のボーナス設定
 *------------------------------------------
 */
// TODO: in each pc_bonus*, purge all 'type' not used by scripts
int pc_bonus(struct map_session_data *sd, SP type, int val)
{
    nullpo_ret(sd);

    switch (type)
    {
        case SP_STR:
        case SP_AGI:
        case SP_VIT:
        case SP_INT:
        case SP_DEX:
        case SP_LUK:
            if (sd->state.lr_flag != 2)
                sd->parame[sp_to_attr(type)] += val;
            break;
        case SP_ATK1:
            if (!sd->state.lr_flag)
                sd->watk += val;
            else if (sd->state.lr_flag == 1)
                sd->watk_ += val;
            break;
        case SP_ATK2:
            if (!sd->state.lr_flag)
                sd->watk2 += val;
            else if (sd->state.lr_flag == 1)
                sd->watk_2 += val;
            break;
        case SP_BASE_ATK:
            if (sd->state.lr_flag != 2)
                sd->base_atk += val;
            break;
        case SP_MATK1:
            if (sd->state.lr_flag != 2)
                sd->matk1 += val;
            break;
        case SP_MATK2:
            if (sd->state.lr_flag != 2)
                sd->matk2 += val;
            break;
        case SP_MATK:
            if (sd->state.lr_flag != 2)
            {
                sd->matk1 += val;
                sd->matk2 += val;
            }
            break;
        case SP_DEF1:
            if (sd->state.lr_flag != 2)
                sd->def += val;
            break;
        case SP_MDEF1:
            if (sd->state.lr_flag != 2)
                sd->mdef += val;
            break;
        case SP_MDEF2:
            if (sd->state.lr_flag != 2)
                sd->mdef += val;
            break;
        case SP_HIT:
            if (sd->state.lr_flag != 2)
                sd->hit += val;
            else
                sd->arrow_hit += val;
            break;
        case SP_FLEE1:
            if (sd->state.lr_flag != 2)
                sd->flee += val;
            break;
        case SP_FLEE2:
            if (sd->state.lr_flag != 2)
                sd->flee2 += val * 10;
            break;
        case SP_CRITICAL:
            if (sd->state.lr_flag != 2)
                sd->critical += val * 10;
            else
                sd->arrow_cri += val * 10;
            break;
        case SP_ATKELE:
            if (!sd->state.lr_flag)
                sd->atk_ele = val;
            else if (sd->state.lr_flag == 1)
                sd->atk_ele_ = val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_ele = val;
            break;
        case SP_DEFELE:
            if (sd->state.lr_flag != 2)
                sd->def_ele = val;
            break;
        case SP_MAXHP:
            if (sd->state.lr_flag != 2)
                sd->status.max_hp += val;
            break;
        case SP_MAXSP:
            if (sd->state.lr_flag != 2)
                sd->status.max_sp += val;
            break;
        case SP_CASTRATE:
            if (sd->state.lr_flag != 2)
                sd->castrate += val;
            break;
        case SP_MAXHPRATE:
            if (sd->state.lr_flag != 2)
                sd->hprate += val;
            break;
        case SP_MAXSPRATE:
            if (sd->state.lr_flag != 2)
                sd->sprate += val;
            break;
        case SP_SPRATE:
            if (sd->state.lr_flag != 2)
                sd->dsprate += val;
            break;
        case SP_ATTACKRANGE:
            if (!sd->state.lr_flag)
                sd->attackrange += val;
            else if (sd->state.lr_flag == 1)
                sd->attackrange_ += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_range += val;
            break;
        case SP_ADD_SPEED:
            if (sd->state.lr_flag != 2)
                sd->speed -= val;
            break;
        case SP_SPEED_RATE:
            if (sd->state.lr_flag != 2)
            {
                if (sd->speed_rate > 100 - val)
                    sd->speed_rate = 100 - val;
            }
            break;
        case SP_SPEED_ADDRATE:
            if (sd->state.lr_flag != 2)
                sd->speed_add_rate = sd->speed_add_rate * (100 - val) / 100;
            break;
        case SP_ASPD:
            if (sd->state.lr_flag != 2)
                sd->aspd -= val * 10;
            break;
        case SP_ASPD_RATE:
            if (sd->state.lr_flag != 2)
            {
                if (sd->aspd_rate > 100 - val)
                    sd->aspd_rate = 100 - val;
            }
            break;
        case SP_ASPD_ADDRATE:
            if (sd->state.lr_flag != 2)
                sd->aspd_add_rate = sd->aspd_add_rate * (100 - val) / 100;
            break;
        case SP_HP_RECOV_RATE:
            if (sd->state.lr_flag != 2)
                sd->hprecov_rate += val;
            break;
        case SP_SP_RECOV_RATE:
            if (sd->state.lr_flag != 2)
                sd->sprecov_rate += val;
            break;
        case SP_CRITICAL_DEF:
            if (sd->state.lr_flag != 2)
                sd->critical_def += val;
            break;
        case SP_NEAR_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->near_attack_def_rate += val;
            break;
        case SP_LONG_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->long_attack_def_rate += val;
            break;
        case SP_DOUBLE_RATE:
            if (sd->state.lr_flag == 0 && sd->double_rate < val)
                sd->double_rate = val;
            break;
        case SP_DOUBLE_ADD_RATE:
            if (sd->state.lr_flag == 0)
                sd->double_add_rate += val;
            break;
        case SP_MATK_RATE:
            if (sd->state.lr_flag != 2)
                sd->matk_rate += val;
            break;
        case SP_IGNORE_DEF_ELE:
            if (!sd->state.lr_flag)
                sd->ignore_def_ele |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->ignore_def_ele_ |= 1 << val;
            break;
        case SP_IGNORE_DEF_RACE:
            if (!sd->state.lr_flag)
                sd->ignore_def_race |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->ignore_def_race_ |= 1 << val;
            break;
        case SP_ATK_RATE:
            if (sd->state.lr_flag != 2)
                sd->atk_rate += val;
            break;
        case SP_MAGIC_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->magic_def_rate += val;
            break;
        case SP_MISC_ATK_DEF:
            if (sd->state.lr_flag != 2)
                sd->misc_def_rate += val;
            break;
        case SP_IGNORE_MDEF_ELE:
            if (sd->state.lr_flag != 2)
                sd->ignore_mdef_ele |= 1 << val;
            break;
        case SP_IGNORE_MDEF_RACE:
            if (sd->state.lr_flag != 2)
                sd->ignore_mdef_race |= 1 << val;
            break;
        case SP_PERFECT_HIT_RATE:
            if (sd->state.lr_flag != 2 && sd->perfect_hit < val)
                sd->perfect_hit = val;
            break;
        case SP_PERFECT_HIT_ADD_RATE:
            if (sd->state.lr_flag != 2)
                sd->perfect_hit_add += val;
            break;
        case SP_CRITICAL_RATE:
            if (sd->state.lr_flag != 2)
                sd->critical_rate += val;
            break;
        case SP_GET_ZENY_NUM:
            if (sd->state.lr_flag != 2 && sd->get_zeny_num < val)
                sd->get_zeny_num = val;
            break;
        case SP_ADD_GET_ZENY_NUM:
            if (sd->state.lr_flag != 2)
                sd->get_zeny_add_num += val;
            break;
        case SP_DEF_RATIO_ATK_ELE:
            if (!sd->state.lr_flag)
                sd->def_ratio_atk_ele |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->def_ratio_atk_ele_ |= 1 << val;
            break;
        case SP_DEF_RATIO_ATK_RACE:
            if (!sd->state.lr_flag)
                sd->def_ratio_atk_race |= 1 << val;
            else if (sd->state.lr_flag == 1)
                sd->def_ratio_atk_race_ |= 1 << val;
            break;
        case SP_HIT_RATE:
            if (sd->state.lr_flag != 2)
                sd->hit_rate += val;
            break;
        case SP_FLEE_RATE:
            if (sd->state.lr_flag != 2)
                sd->flee_rate += val;
            break;
        case SP_FLEE2_RATE:
            if (sd->state.lr_flag != 2)
                sd->flee2_rate += val;
            break;
        case SP_DEF_RATE:
            if (sd->state.lr_flag != 2)
                sd->def_rate += val;
            break;
        case SP_DEF2_RATE:
            if (sd->state.lr_flag != 2)
                sd->def2_rate += val;
            break;
        case SP_MDEF_RATE:
            if (sd->state.lr_flag != 2)
                sd->mdef_rate += val;
            break;
        case SP_MDEF2_RATE:
            if (sd->state.lr_flag != 2)
                sd->mdef2_rate += val;
            break;
        case SP_RESTART_FULL_RECORVER:
            if (sd->state.lr_flag != 2)
                sd->special_state.restart_full_recover = 1;
            break;
        case SP_NO_CASTCANCEL:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_castcancel = 1;
            break;
        case SP_NO_CASTCANCEL2:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_castcancel2 = 1;
            break;
        case SP_NO_SIZEFIX:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_sizefix = 1;
            break;
        case SP_NO_MAGIC_DAMAGE:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_magic_damage = 1;
            break;
        case SP_NO_WEAPON_DAMAGE:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_weapon_damage = 1;
            break;
        case SP_NO_GEMSTONE:
            if (sd->state.lr_flag != 2)
                sd->special_state.no_gemstone = 1;
            break;
        case SP_INFINITE_ENDURE:
            if (sd->state.lr_flag != 2)
                sd->special_state.infinite_endure = 1;
            break;
        case SP_SPLASH_RANGE:
            if (sd->state.lr_flag != 2 && sd->splash_range < val)
                sd->splash_range = val;
            break;
        case SP_SPLASH_ADD_RANGE:
            if (sd->state.lr_flag != 2)
                sd->splash_add_range += val;
            break;
        case SP_SHORT_WEAPON_DAMAGE_RETURN:
            if (sd->state.lr_flag != 2)
                sd->short_weapon_damage_return += val;
            break;
        case SP_LONG_WEAPON_DAMAGE_RETURN:
            if (sd->state.lr_flag != 2)
                sd->long_weapon_damage_return += val;
            break;
        case SP_MAGIC_DAMAGE_RETURN:   //AppleGirl Was Here
            if (sd->state.lr_flag != 2)
                sd->magic_damage_return += val;
            break;
        case SP_ALL_STATS:     // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                for (ATTR attr : ATTRs)
                    sd->parame[attr] += val;
                clif_updatestatus(sd, SP_STR);
                clif_updatestatus(sd, SP_AGI);
                clif_updatestatus(sd, SP_VIT);
                clif_updatestatus(sd, SP_INT);
                clif_updatestatus(sd, SP_DEX);
                clif_updatestatus(sd, SP_LUK);
            }
            break;
        case SP_AGI_VIT:       // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->parame[ATTR::AGI] += val;
                sd->parame[ATTR::VIT] += val;
                clif_updatestatus(sd, SP_AGI);
                clif_updatestatus(sd, SP_VIT);
            }
            break;
        case SP_AGI_DEX_STR:   // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->parame[ATTR::AGI] += val;
                sd->parame[ATTR::DEX] += val;
                sd->parame[ATTR::STR] += val;
                clif_updatestatus(sd, SP_AGI);
                clif_updatestatus(sd, SP_DEX);
                clif_updatestatus(sd, SP_STR);
            }
            break;
        case SP_PERFECT_HIDE:  // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->perfect_hiding = 1;
            }
            break;
        case SP_UNBREAKABLE:
            if (sd->state.lr_flag != 2)
            {
                sd->unbreakable += val;
            }
            break;
        case SP_DEAF:
            sd->special_state.deaf = 1;
            break;
        default:
            if (battle_config.error_log)
                PRINTF("pc_bonus: unknown type %d %d !\n",
                        type, val);
            break;
    }
    return 0;
}

/*==========================================
 * ｿｽｿｽ ｿｽｿｽｿｽiｿｽﾉゑｿｽｿｽｿｽｿｽ\ｿｽﾍ難ｿｽｿｽﾌボｿｽ[ｿｽiｿｽXｿｽﾝ抵ｿｽ
 *------------------------------------------
 */
int pc_bonus2(struct map_session_data *sd, SP type, int type2, int val)
{
    int i;

    nullpo_ret(sd);

    switch (type)
    {
        case SP_ADDELE:
            if (!sd->state.lr_flag)
                sd->addele[type2] += val;
            else if (sd->state.lr_flag == 1)
                sd->addele_[type2] += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_addele[type2] += val;
            break;
        case SP_ADDRACE:
            if (!sd->state.lr_flag)
                sd->addrace[type2] += val;
            else if (sd->state.lr_flag == 1)
                sd->addrace_[type2] += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_addrace[type2] += val;
            break;
        case SP_ADDSIZE:
            if (!sd->state.lr_flag)
                sd->addsize[type2] += val;
            else if (sd->state.lr_flag == 1)
                sd->addsize_[type2] += val;
            else if (sd->state.lr_flag == 2)
                sd->arrow_addsize[type2] += val;
            break;
        case SP_SUBELE:
            if (sd->state.lr_flag != 2)
                sd->subele[type2] += val;
            break;
        case SP_SUBRACE:
            if (sd->state.lr_flag != 2)
                sd->subrace[type2] += val;
            break;
        case SP_ADDEFF:
            if (sd->state.lr_flag != 2)
                sd->addeff[BadSC(type2)] += val;
            else
                sd->arrow_addeff[BadSC(type2)] += val;
            break;
        case SP_ADDEFF2:
            if (sd->state.lr_flag != 2)
                sd->addeff2[BadSC(type2)] += val;
            else
                sd->arrow_addeff2[BadSC(type2)] += val;
            break;
        case SP_RESEFF:
            if (sd->state.lr_flag != 2)
                sd->reseff[BadSC(type2)] += val;
            break;
        case SP_MAGIC_ADDELE:
            if (sd->state.lr_flag != 2)
                sd->magic_addele[type2] += val;
            break;
        case SP_MAGIC_ADDRACE:
            if (sd->state.lr_flag != 2)
                sd->magic_addrace[type2] += val;
            break;
        case SP_MAGIC_SUBRACE:
            if (sd->state.lr_flag != 2)
                sd->magic_subrace[type2] += val;
            break;
        case SP_ADD_DAMAGE_CLASS:
            if (!sd->state.lr_flag)
            {
                for (i = 0; i < sd->add_damage_class_count; i++)
                {
                    if (sd->add_damage_classid[i] == type2)
                    {
                        sd->add_damage_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_damage_class_count
                    && sd->add_damage_class_count < 10)
                {
                    sd->add_damage_classid[sd->add_damage_class_count] =
                        type2;
                    sd->add_damage_classrate[sd->add_damage_class_count] +=
                        val;
                    sd->add_damage_class_count++;
                }
            }
            else if (sd->state.lr_flag == 1)
            {
                for (i = 0; i < sd->add_damage_class_count_; i++)
                {
                    if (sd->add_damage_classid_[i] == type2)
                    {
                        sd->add_damage_classrate_[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_damage_class_count_
                    && sd->add_damage_class_count_ < 10)
                {
                    sd->add_damage_classid_[sd->add_damage_class_count_] =
                        type2;
                    sd->add_damage_classrate_[sd->add_damage_class_count_] +=
                        val;
                    sd->add_damage_class_count_++;
                }
            }
            break;
        case SP_ADD_MAGIC_DAMAGE_CLASS:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->add_magic_damage_class_count; i++)
                {
                    if (sd->add_magic_damage_classid[i] == type2)
                    {
                        sd->add_magic_damage_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_magic_damage_class_count
                    && sd->add_magic_damage_class_count < 10)
                {
                    sd->add_magic_damage_classid
                        [sd->add_magic_damage_class_count] = type2;
                    sd->add_magic_damage_classrate
                        [sd->add_magic_damage_class_count] += val;
                    sd->add_magic_damage_class_count++;
                }
            }
            break;
        case SP_ADD_DEF_CLASS:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->add_def_class_count; i++)
                {
                    if (sd->add_def_classid[i] == type2)
                    {
                        sd->add_def_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_def_class_count
                    && sd->add_def_class_count < 10)
                {
                    sd->add_def_classid[sd->add_def_class_count] = type2;
                    sd->add_def_classrate[sd->add_def_class_count] += val;
                    sd->add_def_class_count++;
                }
            }
            break;
        case SP_ADD_MDEF_CLASS:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->add_mdef_class_count; i++)
                {
                    if (sd->add_mdef_classid[i] == type2)
                    {
                        sd->add_mdef_classrate[i] += val;
                        break;
                    }
                }
                if (i >= sd->add_mdef_class_count
                    && sd->add_mdef_class_count < 10)
                {
                    sd->add_mdef_classid[sd->add_mdef_class_count] = type2;
                    sd->add_mdef_classrate[sd->add_mdef_class_count] += val;
                    sd->add_mdef_class_count++;
                }
            }
            break;
        case SP_HP_DRAIN_RATE:
            if (!sd->state.lr_flag)
            {
                sd->hp_drain_rate += type2;
                sd->hp_drain_per += val;
            }
            else if (sd->state.lr_flag == 1)
            {
                sd->hp_drain_rate_ += type2;
                sd->hp_drain_per_ += val;
            }
            break;
        case SP_SP_DRAIN_RATE:
            if (!sd->state.lr_flag)
            {
                sd->sp_drain_rate += type2;
                sd->sp_drain_per += val;
            }
            else if (sd->state.lr_flag == 1)
            {
                sd->sp_drain_rate_ += type2;
                sd->sp_drain_per_ += val;
            }
            break;
        case SP_WEAPON_COMA_ELE:
            if (sd->state.lr_flag != 2)
                sd->weapon_coma_ele[type2] += val;
            break;
        case SP_WEAPON_COMA_RACE:
            if (sd->state.lr_flag != 2)
                sd->weapon_coma_race[type2] += val;
            break;
        case SP_RANDOM_ATTACK_INCREASE:    // [Valaris]
            if (sd->state.lr_flag != 2)
            {
                sd->random_attack_increase_add = type2;
                sd->random_attack_increase_per += val;
                break;
            }                   // end addition
            break;
        default:
            if (battle_config.error_log)
                PRINTF("pc_bonus2: unknown type %d %d %d!\n",
                        type, type2, val);
            break;
    }
    return 0;
}

int pc_bonus3(struct map_session_data *sd, SP type, int type2, int type3,
               int val)
{
    int i;
    switch (type)
    {
        case SP_ADD_MONSTER_DROP_ITEM:
            if (sd->state.lr_flag != 2)
            {
                for (i = 0; i < sd->monster_drop_item_count; i++)
                {
                    if (sd->monster_drop_itemid[i] == type2)
                    {
                        sd->monster_drop_race[i] |= 1 << type3;
                        if (sd->monster_drop_itemrate[i] < val)
                            sd->monster_drop_itemrate[i] = val;
                        break;
                    }
                }
                if (i >= sd->monster_drop_item_count
                    && sd->monster_drop_item_count < 10)
                {
                    sd->monster_drop_itemid[sd->monster_drop_item_count] =
                        type2;
                    sd->monster_drop_race[sd->monster_drop_item_count] |=
                        1 << type3;
                    sd->monster_drop_itemrate[sd->monster_drop_item_count] =
                        val;
                    sd->monster_drop_item_count++;
                }
            }
            break;
        case SP_AUTOSPELL:
            if (sd->state.lr_flag != 2)
            {
                sd->autospell_id = SkillID(type2);
                sd->autospell_lv = type3;
                sd->autospell_rate = val;
            }
            break;
        default:
            if (battle_config.error_log)
                PRINTF("pc_bonus3: unknown type %d %d %d %d!\n",
                        type, type2, type3, val);
            break;
    }

    return 0;
}

/*==========================================
 * スクリプトによるスキル所得
 *------------------------------------------
 */
int pc_skill(struct map_session_data *sd, SkillID id, int level, int flag)
{
    nullpo_ret(sd);

    if (level > MAX_SKILL_LEVEL)
    {
        if (battle_config.error_log)
            PRINTF("support card skill only!\n");
        return 0;
    }
    if (!flag && (sd->status.skill[id].id == id || level == 0))
    {                           // クエスト所得ならここで条件を確認して送信する
        sd->status.skill[id].lv = level;
        pc_calcstatus(sd, 0);
        clif_skillinfoblock(sd);
    }
    else if (sd->status.skill[id].lv < level)
    {                           // 覚えられるがlvが小さいなら
        sd->status.skill[id].id = id;
        sd->status.skill[id].lv = level;
    }

    return 0;
}


/*==========================================
 * アイテムを買った時に、新しいアイテム欄を使うか、
 * 3万個制限にかかるか確認
 *------------------------------------------
 */
ADDITEM pc_checkadditem(struct map_session_data *sd, int nameid, int amount)
{
    int i;

    nullpo_retr(ADDITEM::ZERO, sd);

    if (itemdb_isequip(nameid))
        return ADDITEM_NEW;

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid == nameid)
        {
            if (sd->status.inventory[i].amount + amount > MAX_AMOUNT)
                return ADDITEM_OVERAMOUNT;
            return ADDITEM_EXIST;
        }
    }

    if (amount > MAX_AMOUNT)
        return ADDITEM_OVERAMOUNT;
    return ADDITEM_NEW;
}

/*==========================================
 * 空きアイテム欄の個数
 *------------------------------------------
 */
int pc_inventoryblank(struct map_session_data *sd)
{
    int i, b;

    nullpo_ret(sd);

    for (i = 0, b = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid == 0)
            b++;
    }

    return b;
}

/*==========================================
 * お金を払う
 *------------------------------------------
 */
int pc_payzeny(struct map_session_data *sd, int zeny)
{
    double z;

    nullpo_ret(sd);

    z = (double) sd->status.zeny;
    if (sd->status.zeny < zeny || z - (double) zeny > MAX_ZENY)
        return 1;
    sd->status.zeny -= zeny;
    clif_updatestatus(sd, SP_ZENY);

    return 0;
}

/*==========================================
 * お金を得る
 *------------------------------------------
 */
int pc_getzeny(struct map_session_data *sd, int zeny)
{
    double z;

    nullpo_ret(sd);

    z = (double) sd->status.zeny;
    if (z + (double) zeny > MAX_ZENY)
    {
        zeny = 0;
        sd->status.zeny = MAX_ZENY;
    }
    sd->status.zeny += zeny;
    clif_updatestatus(sd, SP_ZENY);

    return 0;
}

/*==========================================
 * アイテムを探して、インデックスを返す
 *------------------------------------------
 */
int pc_search_inventory(struct map_session_data *sd, int item_id)
{
    int i;

    nullpo_retr(-1, sd);

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (sd->status.inventory[i].nameid == item_id &&
            (sd->status.inventory[i].amount > 0 || item_id == 0))
            return i;
    }

    return -1;
}

int pc_count_all_items(struct map_session_data *player, int item_id)
{
    int i;
    int count = 0;

    nullpo_ret(player);

    for (i = 0; i < MAX_INVENTORY; i++)
    {
        if (player->status.inventory[i].nameid == item_id)
            count += player->status.inventory[i].amount;
    }

    return count;
}

int pc_remove_items(struct map_session_data *player, int item_id, int count)
{
    int i;

    nullpo_ret(player);

    for (i = 0; i < MAX_INVENTORY && count; i++)
    {
        if (player->status.inventory[i].nameid == item_id)
        {
            int to_delete = count;
            /* only delete as much as we have */
            if (to_delete > player->status.inventory[i].amount)
                to_delete = player->status.inventory[i].amount;

            count -= to_delete;

            pc_delitem(player, i, to_delete,
                        0 /* means `really delete and update status' */ );

            if (!count)
                return 0;
        }
    }
    return 0;
}

/*==========================================
 * アイテム追加。個数のみitem構造体の数字を無視
 *------------------------------------------
 */
PickupFail pc_additem(struct map_session_data *sd, struct item *item_data,
                int amount)
{
    struct item_data *data;
    int i, w;

    MAP_LOG_PC(sd, "PICKUP %d %d", item_data->nameid, amount);

    nullpo_retr(PickupFail::BAD_ITEM, sd);
    nullpo_retr(PickupFail::BAD_ITEM, item_data);

    if (item_data->nameid <= 0 || amount <= 0)
        return PickupFail::BAD_ITEM;
    data = itemdb_search(item_data->nameid);
    if ((w = data->weight * amount) + sd->weight > sd->max_weight)
        return PickupFail::TOO_HEAVY;

    i = MAX_INVENTORY;

    if (!itemdb_isequip2(data))
    {
        // 装 備品ではないので、既所有品なら個数のみ変化させる
        for (i = 0; i < MAX_INVENTORY; i++)
            if (sd->status.inventory[i].nameid == item_data->nameid &&
                sd->status.inventory[i].card[0] == item_data->card[0]
                && sd->status.inventory[i].card[1] == item_data->card[1]
                && sd->status.inventory[i].card[2] == item_data->card[2]
                && sd->status.inventory[i].card[3] == item_data->card[3])
            {
                if (sd->status.inventory[i].amount + amount > MAX_AMOUNT)
                    return PickupFail::STACK_FULL;
                sd->status.inventory[i].amount += amount;
                clif_additem(sd, i, amount, PickupFail::OKAY);
                break;
            }
    }
    if (i >= MAX_INVENTORY)
    {
        // 装 備品か未所有品だったので空き欄へ追加
        i = pc_search_inventory(sd, 0);
        if (i >= 0)
        {
            memcpy(&sd->status.inventory[i], item_data,
                    sizeof(sd->status.inventory[0]));

            if (bool(item_data->equip))
                sd->status.inventory[i].equip = EPOS::ZERO;

            sd->status.inventory[i].amount = amount;
            sd->inventory_data[i] = data;
            clif_additem(sd, i, amount, PickupFail::OKAY);
        }
        else
            return PickupFail::INV_FULL;
    }
    sd->weight += w;
    clif_updatestatus(sd, SP_WEIGHT);

    return PickupFail::OKAY;
}

/*==========================================
 * アイテムを減らす
 *------------------------------------------
 */
int pc_delitem(struct map_session_data *sd, int n, int amount, int type)
{
    nullpo_retr(1, sd);

    if (sd->trade_partner != 0)
        trade_tradecancel(sd);

    if (sd->status.inventory[n].nameid == 0 || amount <= 0
        || sd->status.inventory[n].amount < amount
        || sd->inventory_data[n] == NULL)
        return 1;

    sd->status.inventory[n].amount -= amount;
    sd->weight -= sd->inventory_data[n]->weight * amount;
    if (sd->status.inventory[n].amount <= 0)
    {
        if (bool(sd->status.inventory[n].equip))
            pc_unequipitem(sd, n, CalcStatus::NOW);
        memset(&sd->status.inventory[n], 0,
                sizeof(sd->status.inventory[0]));
        sd->inventory_data[n] = NULL;
    }
    if (!(type & 1))
        clif_delitem(sd, n, amount);
    if (!(type & 2))
        clif_updatestatus(sd, SP_WEIGHT);

    return 0;
}

/*==========================================
 * アイテムを落す
 *------------------------------------------
 */
int pc_dropitem(struct map_session_data *sd, int n, int amount)
{
    nullpo_retr(1, sd);

    if (sd->trade_partner != 0 || sd->npc_id != 0 || sd->state.storage_open)
        return 0;               // no dropping while trading/npc/storage

    if (n < 0 || n >= MAX_INVENTORY)
        return 0;

    if (amount <= 0)
        return 0;

    pc_unequipinvyitem(sd, n, CalcStatus::NOW);

    if (sd->status.inventory[n].nameid <= 0 ||
        sd->status.inventory[n].amount < amount ||
        sd->trade_partner != 0 || sd->status.inventory[n].amount <= 0)
        return 1;
    map_addflooritem(&sd->status.inventory[n], amount, sd->bl.m, sd->bl.x,
                      sd->bl.y, NULL, NULL, NULL, 0);
    pc_delitem(sd, n, amount, 0);

    return 0;
}

/*==========================================
 * アイテムを拾う
 *------------------------------------------
 */

static
int can_pick_item_up_from(struct map_session_data *self, int other_id)
{
    struct party *p = party_search(self->status.party_id);

    /* From ourselves or from no-one? */
    if (!self || self->bl.id == other_id || !other_id)
        return 1;

    struct map_session_data *other = map_id2sd(other_id);

    /* Other no longer exists? */
    if (!other)
        return 1;

    /* From our partner? */
    if (self->status.partner_id == other->status.char_id)
        return 1;

    /* From a party member? */
    if (self->status.party_id
        && self->status.party_id == other->status.party_id
        && p && p->item != 0)
        return 1;

    /* From someone who is far away? */
    /* On another map? */
    if (other->bl.m != self->bl.m)
        return 1;
    else
    {
        int distance_x = abs(other->bl.x - self->bl.x);
        int distance_y = abs(other->bl.y - self->bl.y);
        int distance = (distance_x > distance_y) ? distance_x : distance_y;

        return distance > battle_config.drop_pickup_safety_zone;
    }
}

int pc_takeitem(struct map_session_data *sd, struct flooritem_data *fitem)
{
    unsigned int tick = gettick();
    int can_take;

    nullpo_ret(sd);
    nullpo_ret(fitem);

    /* Sometimes the owners reported to us are buggy: */

    if (fitem->first_get_id == fitem->third_get_id
        || fitem->second_get_id == fitem->third_get_id)
        fitem->third_get_id = 0;

    if (fitem->first_get_id == fitem->second_get_id)
    {
        fitem->second_get_id = fitem->third_get_id;
        fitem->third_get_id = 0;
    }

    can_take = can_pick_item_up_from(sd, fitem->first_get_id);
    if (!can_take)
        can_take = fitem->first_get_tick <= tick
            && can_pick_item_up_from(sd, fitem->second_get_id);

    if (!can_take)
        can_take = fitem->second_get_tick <= tick
            && can_pick_item_up_from(sd, fitem->third_get_id);

    if (!can_take)
        can_take = fitem->third_get_tick <= tick;

    if (can_take)
    {
        /* Can pick up */

        PickupFail flag = pc_additem(sd, &fitem->item_data, fitem->item_data.amount);
        if (flag != PickupFail::OKAY)
            // 重量overで取得失敗
            clif_additem(sd, 0, 0, flag);
        else
        {
            // 取得成功
            if (sd->attacktimer != -1)
                pc_stopattack(sd);
            clif_takeitem(&sd->bl, &fitem->bl);
            map_clearflooritem(fitem->bl.id);
        }
        return 0;
    }

    /* Otherwise, we can't pick up */
    clif_additem(sd, 0, 0, PickupFail::DROP_STEAL);
    return 0;
}

static
int pc_isUseitem(struct map_session_data *sd, int n)
{
    struct item_data *item;
    int nameid;

    nullpo_ret(sd);

    item = sd->inventory_data[n];
    nameid = sd->status.inventory[n].nameid;

    if (item == NULL)
        return 0;
    if (itemdb_type(nameid) != ItemType::USE)
        return 0;
    if (nameid == 601
        && (map[sd->bl.m].flag.noteleport))
    {
        return 0;
    }

    if (nameid == 602 && map[sd->bl.m].flag.noreturn)
        return 0;
    if (nameid == 604
        && (map[sd->bl.m].flag.nobranch))
        return 0;
    if (item->sex != 2 && sd->status.sex != item->sex)
        return 0;
    if (item->elv > 0 && sd->status.base_level < item->elv)
        return 0;

    return 1;
}

/*==========================================
 * アイテムを使う
 *------------------------------------------
 */
int pc_useitem(struct map_session_data *sd, int n)
{
    int amount;

    nullpo_retr(1, sd);

    if (n >= 0 && n < MAX_INVENTORY && sd->inventory_data[n])
    {
        amount = sd->status.inventory[n].amount;
        if (sd->status.inventory[n].nameid <= 0
            || sd->status.inventory[n].amount <= 0
            || sd->sc_data[SC_BERSERK].timer != -1 || !pc_isUseitem(sd, n))
        {
            clif_useitemack(sd, n, 0, 0);
            return 1;
        }

        run_script(sd->inventory_data[n]->use_script, 0, sd->bl.id, 0);

        clif_useitemack(sd, n, amount - 1, 1);
        pc_delitem(sd, n, 1, 1);
    }

    return 0;
}

/*==========================================
 * カートアイテムを減らす
 *------------------------------------------
 */
static
int pc_cart_delitem(struct map_session_data *sd, int n, int amount, int)
{
    nullpo_retr(1, sd);

    if (sd->status.cart[n].nameid == 0 || sd->status.cart[n].amount < amount)
        return 1;

    sd->status.cart[n].amount -= amount;
    sd->cart_weight -= itemdb_weight(sd->status.cart[n].nameid) * amount;
    if (sd->status.cart[n].amount <= 0)
    {
        memset(&sd->status.cart[n], 0, sizeof(sd->status.cart[0]));
        sd->cart_num--;
    }

    return 0;
}

/*==========================================
 * スティル品公開
 *------------------------------------------
 */
static
void pc_show_steal(struct block_list *bl,
        struct map_session_data *sd, int itemid, int type)
{
    nullpo_retv(bl);
    nullpo_retv(sd);

    std::string output;
    if (!type)
    {
        struct item_data *item = itemdb_exists(itemid);
        if (item == NULL)
            output = STRPRINTF("%s stole an Unknown_Item.",
                    sd->status.name);
        else
            output = STRPRINTF("%s stole %s.",
                    sd->status.name, item->jname);
        clif_displaymessage(((struct map_session_data *) bl)->fd, output);
    }
    else
    {
        output = STRPRINTF(
                "%s has not stolen the item because of being  overweight.",
                sd->status.name);
        clif_displaymessage(((struct map_session_data *) bl)->fd, output);
    }
}

/*==========================================
 *
 *------------------------------------------
 */
//** pc.c: Small Steal Item fix by fritz
int pc_steal_item(struct map_session_data *sd, struct block_list *bl)
{
    if (sd != NULL && bl != NULL && bl->type == BL_MOB)
    {
        int i, skill, rate, itemid, count;
        struct mob_data *md;
        md = (struct mob_data *) bl;
        if (!md->state.steal_flag && mob_db[md->mob_class].mexp <= 0 &&
            !(mob_db[md->mob_class].mode & 0x20) &&
            md->sc_data[SC_STONE].timer == -1 &&
            md->sc_data[SC_FREEZE].timer == -1 &&
            (!(md->mob_class > 1324 && md->mob_class < 1364)))   // prevent stealing from treasure boxes [Valaris]
        {
            skill = sd->paramc[ATTR::DEX] - mob_db[md->mob_class].attrs[ATTR::DEX] + 10;

            if (0 < skill)
            {
                for (count = 8; count <= 8 && count != 0; count--)
                {
                    i = rand() % 8;
                    itemid = mob_db[md->mob_class].dropitem[i].nameid;

                    if (itemid > 0 && itemdb_type(itemid) != ItemType::_6)
                    {
                        rate =
                            (mob_db[md->mob_class].dropitem[i].p /
                             battle_config.item_rate_common * 100 * skill) /
                            100;

                        if (rand() % 10000 < rate)
                        {
                            struct item tmp_item;
                            memset(&tmp_item, 0, sizeof(tmp_item));
                            tmp_item.nameid = itemid;
                            tmp_item.amount = 1;
                            tmp_item.identify = 1;
                            PickupFail flag = pc_additem(sd, &tmp_item, 1);
                            if (battle_config.show_steal_in_same_party)
                            {
                                party_foreachsamemap(
                                        std::bind(pc_show_steal, ph::_1, sd, tmp_item.nameid, 0), sd, 1);
                            }

                            if (flag != PickupFail::OKAY)
                            {
                                if (battle_config.show_steal_in_same_party)
                                {
                                    party_foreachsamemap(
                                            std::bind(pc_show_steal, ph::_1, sd, tmp_item.nameid, 1), sd, 1);
                                }

                                clif_additem(sd, 0, 0, flag);
                            }
                            md->state.steal_flag = 1;
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int pc_steal_coin(struct map_session_data *sd, struct block_list *bl)
{
    if (sd != NULL && bl != NULL && bl->type == BL_MOB)
    {
        int rate;
        struct mob_data *md = (struct mob_data *) bl;
        if (md && !md->state.steal_coin_flag
            && md->sc_data[SC_STONE].timer == -1
            && md->sc_data[SC_FREEZE].timer == -1)
        {
            rate = (sd->status.base_level - mob_db[md->mob_class].lv) * 3
                + sd->paramc[ATTR::DEX] * 2 + sd->paramc[ATTR::LUK] * 2;
            if (MRAND(1000) < rate)
            {
                pc_getzeny(sd, mob_db[md->mob_class].lv * 10 + MRAND(100));
                md->state.steal_coin_flag = 1;
                return 1;
            }
        }
    }

    return 0;
}

//
//
//
/*==========================================
 * PCの位置設定
 *------------------------------------------
 */
int pc_setpos(struct map_session_data *sd, const char *mapname_org, int x, int y,
               int clrtype)
{
    char mapname[24];
    int m = 0, c = 0;

    nullpo_ret(sd);

    if (sd->chatID)             // チャットから出る
        chat_leavechat(sd);
    if (sd->trade_partner)      // 取引を中断する
        trade_tradecancel(sd);
    if (sd->state.storage_open)
        storage_storage_quit(sd);  // 倉庫を開いてるなら保存する

    if (sd->party_invite > 0)   // パーティ勧誘を拒否する
        party_reply_invite(sd, sd->party_invite_account, 0);

    skill_castcancel(&sd->bl, 0);  // 詠唱中断
    pc_stop_walking(sd, 0);    // 歩行中断
    pc_stopattack(sd);         // 攻撃中断

    if (pc_issit(sd))
    {
//        pc_setstand (sd); // [fate] Nothing wrong with warping while sitting
        skill_gangsterparadise(sd, 0);
    }

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)
        skill_status_change_end(&sd->bl, SC_TRICKDEAD, -1);
    if (bool(sd->status.option & Option::HIDE2))
        skill_status_change_end(&sd->bl, SC_HIDING, -1);
    if (bool(sd->status.option & Option::CLOAK))
        skill_status_change_end(&sd->bl, SC_CLOAKING, -1);
    if (bool(sd->status.option & (Option::CHASEWALK | Option::HIDE2)))
        skill_status_change_end(&sd->bl, SC_CHASEWALK, -1);
    if (sd->sc_data[SC_DANCING].timer != -1)    // clear dance effect when warping [Valaris]
        skill_stop_dancing(&sd->bl, 0);

    memcpy(mapname, mapname_org, 24);
    mapname[16] = 0;
    if (strstr(mapname, ".gat") == NULL && strlen(mapname) < 16)
    {
        strcat(mapname, ".gat");
    }

    m = map_mapname2mapid(mapname);
    if (m < 0)
    {
        if (sd->mapname[0])
        {
            struct in_addr ip;
            int port;
            if (map_mapname2ipport(mapname, &ip, &port) == 0)
            {
                skill_stop_dancing(&sd->bl, 1);
                skill_unit_out_all(&sd->bl, gettick(), 1);
                clif_clearchar_area(&sd->bl, clrtype & 0xffff);
                skill_gangsterparadise(sd, 0);
                map_delblock(&sd->bl);
                memcpy(sd->mapname, mapname, 24);
                sd->bl.x = x;
                sd->bl.y = y;
                sd->state.waitingdisconnect = 1;
                pc_makesavestatus(sd);
                //The storage close routines save the char data. [Skotlex]
                if (!sd->state.storage_open)
                    chrif_save(sd);
                else if (sd->state.storage_open)
                    storage_storage_quit(sd);

                chrif_changemapserver(sd, mapname, x, y, ip, port);
                return 0;
            }
        }
#if 0
        clif_authfail_fd(sd->fd, 0);   // cancel
        clif_setwaitclose(sd->fd);
#endif
        return 1;
    }

    if (x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys)
        x = y = 0;
    if ((x == 0 && y == 0) || (c = read_gat(m, x, y)) == 1 || c == 5)
    {
        if (x || y)
        {
            if (battle_config.error_log)
                PRINTF("stacked (%d,%d)\n", x, y);
        }
        do
        {
            x = MRAND(map[m].xs - 2) + 1;
            y = MRAND(map[m].ys - 2) + 1;
        }
        while ((c = read_gat(m, x, y)) == 1 || c == 5);
    }

    if (sd->mapname[0] && sd->bl.prev != NULL)
    {
        skill_unit_out_all(&sd->bl, gettick(), 1);
        clif_clearchar_area(&sd->bl, clrtype & 0xffff);
        skill_gangsterparadise(sd, 0);
        map_delblock(&sd->bl);
        clif_changemap(sd, map[m].name, x, y); // [MouseJstr]
    }

    memcpy(sd->mapname, mapname, 24);
    sd->bl.m = m;
    sd->to_x = x;
    sd->to_y = y;

    // moved and changed dance effect stopping

    sd->bl.x = x;
    sd->bl.y = y;

//  map_addblock(&sd->bl);  // ブロック登録とspawnは
//  clif_spawnpc(sd);

    return 0;
}

/*==========================================
 * PCのランダムワープ
 *------------------------------------------
 */
int pc_randomwarp(struct map_session_data *sd, int type)
{
    int x, y, c, i = 0;
    int m;

    nullpo_ret(sd);

    m = sd->bl.m;

    if (map[sd->bl.m].flag.noteleport)  // テレポート禁止
        return 0;

    do
    {
        x = MRAND(map[m].xs - 2) + 1;
        y = MRAND(map[m].ys - 2) + 1;
    }
    while (((c = read_gat(m, x, y)) == 1 || c == 5) && (i++) < 1000);

    if (i < 1000)
        pc_setpos(sd, map[m].name, x, y, type);

    return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
static
int pc_can_reach(struct map_session_data *sd, int x, int y)
{
    struct walkpath_data wpd;

    nullpo_ret(sd);

    if (sd->bl.x == x && sd->bl.y == y) // 同じマス
        return 1;

    // 障害物判定
    wpd.path_len = 0;
    wpd.path_pos = 0;
    wpd.path_half = 0;
    return (path_search(&wpd, sd->bl.m, sd->bl.x, sd->bl.y, x, y, 0) !=
            -1) ? 1 : 0;
}

//
// 歩 行物
//
/*==========================================
 * 次の1歩にかかる時間を計算
 *------------------------------------------
 */
static
int calc_next_walk_step(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->walkpath.path_pos >= sd->walkpath.path_len)
        return -1;
    if (sd->walkpath.path[sd->walkpath.path_pos] & 1)
        return sd->speed * 14 / 10;

    return sd->speed;
}

/*==========================================
 * 半歩進む(timer関数)
 *------------------------------------------
 */
static
void pc_walk(timer_id tid, tick_t tick, custom_id_t id, custom_data_t data)
{
    struct map_session_data *sd;
    int i, ctype;
    int moveblock;
    int x, y, dx, dy;

    sd = map_id2sd(id);
    if (sd == NULL)
        return;

    if (sd->walktimer != tid)
    {
        if (battle_config.error_log)
            PRINTF("pc_walk %d != %d\n", sd->walktimer, tid);
        return;
    }
    sd->walktimer = -1;
    if (sd->walkpath.path_pos >= sd->walkpath.path_len
        || sd->walkpath.path_pos != data)
        return;

    //歩いたので息吹のタイマーを初期化
    sd->inchealspirithptick = 0;
    sd->inchealspiritsptick = 0;

    sd->walkpath.path_half ^= 1;
    if (sd->walkpath.path_half == 0)
    {                           // マス目中心へ到着
        sd->walkpath.path_pos++;
        if (sd->state.change_walk_target)
        {
            pc_walktoxy_sub(sd);
            return;
        }
    }
    else
    {                           // マス目境界へ到着
        if (sd->walkpath.path[sd->walkpath.path_pos] >= 8)
            return;

        x = sd->bl.x;
        y = sd->bl.y;
        ctype = map_getcell(sd->bl.m, x, y);
        if (ctype == 1 || ctype == 5)
        {
            pc_stop_walking(sd, 1);
            return;
        }
        sd->dir = sd->head_dir = sd->walkpath.path[sd->walkpath.path_pos];
        dx = dirx[(int) sd->dir];
        dy = diry[(int) sd->dir];
        ctype = map_getcell(sd->bl.m, x + dx, y + dy);
        if (ctype == 1 || ctype == 5)
        {
            pc_walktoxy_sub(sd);
            return;
        }

        moveblock = (x / BLOCK_SIZE != (x + dx) / BLOCK_SIZE
                     || y / BLOCK_SIZE != (y + dy) / BLOCK_SIZE);

        sd->walktimer = 1;
        map_foreachinmovearea(std::bind(clif_pcoutsight, ph::_1, sd),
                sd->bl.m, x - AREA_SIZE, y - AREA_SIZE,
                x + AREA_SIZE, y + AREA_SIZE,
                dx, dy,
                BL_NUL);

        x += dx;
        y += dy;

        if (moveblock)
            map_delblock(&sd->bl);
        sd->bl.x = x;
        sd->bl.y = y;
        if (moveblock)
            map_addblock(&sd->bl);

        if (sd->sc_data[SC_DANCING].timer != -1)
            skill_unit_move_unit_group((struct skill_unit_group *)
                                        sd->sc_data[SC_DANCING].val2,
                                        sd->bl.m, dx, dy);

        map_foreachinmovearea(std::bind(clif_pcinsight, ph::_1, sd),
                sd->bl.m, x - AREA_SIZE, y - AREA_SIZE,
                x + AREA_SIZE, y + AREA_SIZE,
                -dx, -dy,
                BL_NUL);
        sd->walktimer = -1;

        if (sd->status.party_id > 0)
        {                       // パーティのＨＰ情報通知検査
            struct party *p = party_search(sd->status.party_id);
            if (p != NULL)
            {
                int p_flag = 0;
                map_foreachinmovearea(std::bind(party_send_hp_check, ph::_1, sd->status.party_id, &p_flag),
                        sd->bl.m, x - AREA_SIZE, y - AREA_SIZE,
                        x + AREA_SIZE, y + AREA_SIZE,
                        -dx, -dy,
                        BL_PC);
                if (p_flag)
                    sd->party_hp = -1;
            }
        }
        // クローキングの消滅検査
        if (bool(sd->status.option & Option::CLOAK))
            skill_check_cloaking(&sd->bl);
        // ディボーション検査
        for (i = 0; i < 5; i++)
            if (sd->dev.val1[i])
            {
                skill_devotion3(&sd->bl, sd->dev.val1[i]);
                break;
            }
        // 被ディボーション検査
        if (sd->sc_data[SC_DEVOTION].val1)
        {
            skill_devotion2(&sd->bl, sd->sc_data[SC_DEVOTION].val1);
        }

        skill_unit_move(&sd->bl, tick, 1); // スキルユニットの検査

        if (map_getcell(sd->bl.m, x, y) & 0x80)
            npc_touch_areanpc(sd, sd->bl.m, x, y);
        else
            sd->areanpc_id = 0;
    }
    if ((i = calc_next_walk_step(sd)) > 0)
    {
        i = i >> 1;
        if (i < 1 && sd->walkpath.path_half == 0)
            i = 1;
        sd->walktimer =
            add_timer(tick + i, pc_walk, id, sd->walkpath.path_pos);
    }
}

/*==========================================
 * 移動可能か確認して、可能なら歩行開始
 *------------------------------------------
 */
static
int pc_walktoxy_sub(struct map_session_data *sd)
{
    struct walkpath_data wpd;
    int i;

    nullpo_retr(1, sd);

    if (path_search(&wpd, sd->bl.m, sd->bl.x, sd->bl.y, sd->to_x, sd->to_y, 0))
        return 1;
    memcpy(&sd->walkpath, &wpd, sizeof(wpd));

    clif_walkok(sd);
    sd->state.change_walk_target = 0;

    if ((i = calc_next_walk_step(sd)) > 0)
    {
        i = i >> 2;
        sd->walktimer = add_timer(gettick() + i, pc_walk, sd->bl.id, 0);
    }
    clif_movechar(sd);

    return 0;
}

/*==========================================
 * pc歩 行要求
 *------------------------------------------
 */
int pc_walktoxy(struct map_session_data *sd, int x, int y)
{

    nullpo_ret(sd);

    sd->to_x = x;
    sd->to_y = y;

    if (pc_issit(sd))
        pc_setstand(sd);

    if (sd->walktimer != -1 && sd->state.change_walk_target == 0)
    {
        // 現在歩いている最中の目的地変更なのでマス目の中心に来た時に
        // timer関数からpc_walktoxy_subを呼ぶようにする
        sd->state.change_walk_target = 1;
    }
    else
    {
        pc_walktoxy_sub(sd);
    }

    return 0;
}

/*==========================================
 * 歩 行停止
 *------------------------------------------
 */
int pc_stop_walking(struct map_session_data *sd, int type)
{
    nullpo_ret(sd);

    if (sd->walktimer != -1)
    {
        delete_timer(sd->walktimer, pc_walk);
        sd->walktimer = -1;
    }
    sd->walkpath.path_len = 0;
    sd->to_x = sd->bl.x;
    sd->to_y = sd->bl.y;
    if (type & 0x01)
        clif_fixpos(&sd->bl);
    if (type & 0x02 && battle_config.pc_damage_delay)
    {
        unsigned int tick = gettick();
        int delay = battle_get_dmotion(&sd->bl);
        if (sd->canmove_tick < tick)
            sd->canmove_tick = tick + delay;
    }

    return 0;
}

void pc_touch_all_relevant_npcs(struct map_session_data *sd)
{
    if (map_getcell(sd->bl.m, sd->bl.x, sd->bl.y) & 0x80)
        npc_touch_areanpc(sd, sd->bl.m, sd->bl.x, sd->bl.y);
    else
        sd->areanpc_id = 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int pc_movepos(struct map_session_data *sd, int dst_x, int dst_y)
{
    int moveblock;
    int dx, dy, dist;

    struct walkpath_data wpd;

    nullpo_ret(sd);

    if (path_search(&wpd, sd->bl.m, sd->bl.x, sd->bl.y, dst_x, dst_y, 0))
        return 1;

    sd->dir = sd->head_dir = map_calc_dir(&sd->bl, dst_x, dst_y);

    dx = dst_x - sd->bl.x;
    dy = dst_y - sd->bl.y;
    dist = distance(sd->bl.x, sd->bl.y, dst_x, dst_y);

    moveblock = (sd->bl.x / BLOCK_SIZE != dst_x / BLOCK_SIZE
                 || sd->bl.y / BLOCK_SIZE != dst_y / BLOCK_SIZE);

    map_foreachinmovearea(std::bind(clif_pcoutsight, ph::_1, sd),
            sd->bl.m, sd->bl.x - AREA_SIZE, sd->bl.y - AREA_SIZE,
            sd->bl.x + AREA_SIZE, sd->bl.y + AREA_SIZE,
            dx, dy,
            BL_NUL);

    if (moveblock)
        map_delblock(&sd->bl);
    sd->bl.x = dst_x;
    sd->bl.y = dst_y;
    if (moveblock)
        map_addblock(&sd->bl);

    map_foreachinmovearea(std::bind(clif_pcinsight, ph::_1, sd),
            sd->bl.m, sd->bl.x - AREA_SIZE, sd->bl.y - AREA_SIZE,
            sd->bl.x + AREA_SIZE, sd->bl.y + AREA_SIZE,
            -dx, -dy,
            BL_NUL);

    if (sd->status.party_id > 0)
    {                           // パーティのＨＰ情報通知検査
        struct party *p = party_search(sd->status.party_id);
        if (p != NULL)
        {
            int flag = 0;
            map_foreachinmovearea(std::bind(party_send_hp_check, ph::_1, sd->status.party_id, &flag),
                    sd->bl.m, sd->bl.x - AREA_SIZE, sd->bl.y - AREA_SIZE,
                    sd->bl.x + AREA_SIZE, sd->bl.y + AREA_SIZE,
                    -dx, -dy,
                    BL_PC);
            if (flag)
                sd->party_hp = -1;
        }
    }

    // クローキングの消滅検査
    if (bool(sd->status.option & Option::CLOAK))
        skill_check_cloaking(&sd->bl);

    skill_unit_move(&sd->bl, gettick(), dist + 7);    // スキルユニットの検査

    pc_touch_all_relevant_npcs(sd);
    return 0;
}

//
// 武器戦闘
//
/*==========================================
 * スキルの検索 所有していた場合Lvが返る
 *------------------------------------------
 */
int pc_checkskill(struct map_session_data *sd, SkillID skill_id)
{
    if (sd == NULL)
        return 0;

    if (sd->status.skill[skill_id].id == skill_id)
        return (sd->status.skill[skill_id].lv);

    return 0;
}

/*==========================================
 * 武器変更によるスキルの継続チェック
 * 引数：
 *   struct map_session_data *sd        セッションデータ
 *   int nameid                                         装備品ID
 * 返り値：
 *   0          変更なし
 *   -1         スキルを解除
 *------------------------------------------
 */
static
int pc_checkallowskill(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->sc_data[SC_TWOHANDQUICKEN].timer != -1)
    {                           // 2HQ
        skill_status_change_end(&sd->bl, SC_TWOHANDQUICKEN, -1);   // 2HQを解除
        return -1;
    }
    if (sd->sc_data[SC_AURABLADE].timer != -1)
    {                           // オーラブレード
        skill_status_change_end(&sd->bl, SC_AURABLADE, -1);    // オーラブレードを解除
        return -1;
    }
    if (sd->sc_data[SC_PARRYING].timer != -1)
    {                           // パリイング
        skill_status_change_end(&sd->bl, SC_PARRYING, -1); // パリイングを解除
        return -1;
    }
    if (sd->sc_data[SC_CONCENTRATION].timer != -1)
    {                           // コンセントレーション
        skill_status_change_end(&sd->bl, SC_CONCENTRATION, -1);    // コンセントレーションを解除
        return -1;
    }
    if (sd->sc_data[SC_SPEARSQUICKEN].timer != -1)
    {                           // スピアクィッケン
        skill_status_change_end(&sd->bl, SC_SPEARSQUICKEN, -1);    // スピアクイッケンを解除
        return -1;
    }
    if (sd->sc_data[SC_ADRENALINE].timer != -1)
    {                           // アドレナリンラッシュ
        skill_status_change_end(&sd->bl, SC_ADRENALINE, -1);   // アドレナリンラッシュを解除
        return -1;
    }

    if (sd->status.shield <= 0)
    {
        if (sd->sc_data[SC_AUTOGUARD].timer != -1)
        {                       // オートガード
            skill_status_change_end(&sd->bl, SC_AUTOGUARD, -1);
            return -1;
        }
        if (sd->sc_data[SC_DEFENDER].timer != -1)
        {                       // ディフェンダー
            skill_status_change_end(&sd->bl, SC_DEFENDER, -1);
            return -1;
        }
        if (sd->sc_data[SC_REFLECTSHIELD].timer != -1)
        {                       //リフレクトシールド
            skill_status_change_end(&sd->bl, SC_REFLECTSHIELD, -1);
            return -1;
        }
    }

    return 0;
}

/*==========================================
 * 装 備品のチェック
 *------------------------------------------
 */
int pc_checkequip(struct map_session_data *sd, EPOS pos)
{
    nullpo_retr(-1, sd);

    for (EQUIP i : EQUIPs)
    {
        if (bool(pos & equip_pos[i]))
            return sd->equip_index[i];
    }

    return -1;
}

/*==========================================
 * PCの攻撃 (timer関数)
 *------------------------------------------
 */
static
void pc_attack_timer(timer_id tid, tick_t tick, custom_id_t id, custom_data_t)
{
    struct map_session_data *sd;
    struct block_list *bl;
    eptr<struct status_change, StatusChange> sc_data;
    int dist, range;
    int attack_spell_delay;

    sd = map_id2sd(id);
    if (sd == NULL)
        return;
    if (sd->attacktimer != tid)
    {
        if (battle_config.error_log)
            PRINTF("pc_attack_timer %d != %d\n", sd->attacktimer, tid);
        return;
    }
    sd->attacktimer = -1;

    if (sd->bl.prev == NULL)
        return;

    bl = map_id2bl(sd->attacktarget);
    if (bl == NULL || bl->prev == NULL)
        return;

    if (bl->type == BL_PC && pc_isdead((struct map_session_data *) bl))
        return;

    // 同じmapでないなら攻撃しない
    // PCが死んでても攻撃しない
    if (sd->bl.m != bl->m || pc_isdead(sd))
        return;

    // 異常などで攻撃できない
    if (sd->opt1 != Opt1::ZERO
        || bool(sd->status.option & (Option::OLD_ANY_HIDE)))
        return;

    Option *opt = battle_get_option(bl);
    if (opt != NULL && bool(*opt & Option::REAL_ANY_HIDE))
        return;
    if (((sc_data = battle_get_sc_data(bl))
         && sc_data[SC_TRICKDEAD].timer != -1)
        || ((sc_data = battle_get_sc_data(bl))
            && sc_data[SC_BASILICA].timer != -1))
        return;

    if (sd->skilltimer != -1)
        return;

    if (!battle_config.sdelay_attack_enable)
    {
        if (DIFF_TICK(tick, sd->canact_tick) < 0)
        {
            clif_skill_fail(sd, SkillID::ONE, 4, 0);
            return;
        }
    }

    if (sd->attackabletime > tick)
        return;               // cannot attack yet

    attack_spell_delay = sd->attack_spell_delay;
    if (sd->attack_spell_override   // [Fate] If we have an active attack spell, use that
        && spell_attack(id, sd->attacktarget))
    {
        // Return if the spell succeeded.  If the spell had disspiated, spell_attack() may fail.
        sd->attackabletime = tick + attack_spell_delay;

    }
    else
    {
        dist = distance(sd->bl.x, sd->bl.y, bl->x, bl->y);
        range = sd->attackrange;
        if (sd->status.weapon != 11)
            range++;
        if (dist > range)
        {                       // 届 かないので移動
            //if(pc_can_reach(sd,bl->x,bl->y))
            //clif_movetoattack(sd,bl);
            return;
        }

        if (dist <= range && !battle_check_range(&sd->bl, bl, range))
        {
            if (pc_can_reach(sd, bl->x, bl->y) && sd->canmove_tick < tick
                && (sd->sc_data[SC_ANKLE].timer == -1
                    || sd->sc_data[SC_SPIDERWEB].timer == -1))
                // TMW client doesn't support this
                //pc_walktoxy(sd,bl->x,bl->y);
                clif_movetoattack(sd, bl);
            sd->attackabletime = tick + (sd->aspd << 1);
        }
        else
        {
            if (battle_config.pc_attack_direction_change)
                sd->dir = sd->head_dir = map_calc_dir(&sd->bl, bl->x, bl->y);  // 向き設定

            if (sd->walktimer != -1)
                pc_stop_walking(sd, 1);

            {
                map_freeblock_lock();
                pc_stop_walking(sd, 0);
                sd->attacktarget_lv =
                    battle_weapon_attack(&sd->bl, bl, tick, BCT_ZERO);
                if (!(battle_config.pc_cloak_check_type & 2)
                    && sd->sc_data[SC_CLOAKING].timer != -1)
                    skill_status_change_end(&sd->bl, SC_CLOAKING, -1);
                map_freeblock_unlock();
                sd->attackabletime = tick + (sd->aspd << 1);
            }
            if (sd->attackabletime <= tick)
                sd->attackabletime = tick + (battle_config.max_aspd << 1);
        }
    }

    if (sd->state.attack_continue)
    {
        sd->attacktimer =
            add_timer(sd->attackabletime, pc_attack_timer, sd->bl.id, 0);
    }
}

/*==========================================
 * 攻撃要求
 * typeが1なら継続攻撃
 *------------------------------------------
 */
int pc_attack(struct map_session_data *sd, int target_id, int type)
{
    struct block_list *bl;
    int d;

    nullpo_ret(sd);

    bl = map_id2bl(target_id);
    if (bl == NULL)
        return 1;

    if (bl->type == BL_NPC)
    {                           // monster npcs [Valaris]
        npc_click(sd, RFIFOL(sd->fd, 2));
        return 0;
    }

    if (!battle_check_target(&sd->bl, bl, BCT_ENEMY))
        return 1;
    if (sd->attacktimer != -1)
        pc_stopattack(sd);
    sd->attacktarget = target_id;
    sd->state.attack_continue = type;

    d = DIFF_TICK(sd->attackabletime, gettick());
    if (d > 0 && d < 2000)
    {                           // 攻撃delay中
        sd->attacktimer =
            add_timer(sd->attackabletime, pc_attack_timer, sd->bl.id, 0);
    }
    else
    {
        // 本来timer関数なので引数を合わせる
        pc_attack_timer(-1, gettick(), sd->bl.id, 0);
    }

    return 0;
}

/*==========================================
 * 継続攻撃停止
 *------------------------------------------
 */
int pc_stopattack(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->attacktimer != -1)
    {
        delete_timer(sd->attacktimer, pc_attack_timer);
        sd->attacktimer = -1;
    }
    sd->attacktarget = 0;
    sd->state.attack_continue = 0;

    return 0;
}

static
int pc_checkbaselevelup(struct map_session_data *sd)
{
    int next = pc_nextbaseexp(sd);

    nullpo_ret(sd);

    if (sd->status.base_exp >= next && next > 0)
    {
        // base側レベルアップ処理
        sd->status.base_exp -= next;

        sd->status.base_level++;
        sd->status.status_point += (sd->status.base_level + 14) / 4;
        clif_updatestatus(sd, SP_STATUSPOINT);
        clif_updatestatus(sd, SP_BASELEVEL);
        clif_updatestatus(sd, SP_NEXTBASEEXP);
        pc_calcstatus(sd, 0);
        pc_heal(sd, sd->status.max_hp, sd->status.max_sp);

        clif_misceffect(&sd->bl, 0);
        //レベルアップしたのでパーティー情報を更新する
        //(公平範囲チェック)
        party_send_movemap(sd);
        MAP_LOG_XP(sd, "LEVELUP");
        return 1;
    }

    return 0;
}

/*========================================
 * Compute the maximum for sd->skill_point, i.e., the max. number of skill points that can still be filled in
 *----------------------------------------
 */
static
int pc_skillpt_potential(struct map_session_data *sd)
{
    int potential = 0;

#define RAISE_COST(x) (((x)*((x)-1))>>1)

    for (SkillID skill_id = SkillID(); skill_id < MAX_SKILL;
            skill_id = SkillID(uint16_t(skill_id) + 1))
        if (sd->status.skill[skill_id].id != SkillID::ZERO
            && sd->status.skill[skill_id].lv < skill_db[skill_id].max_raise)
            potential += RAISE_COST(skill_db[skill_id].max_raise)
                - RAISE_COST(sd->status.skill[skill_id].lv);
#undef RAISE_COST

    return potential;
}

static
int pc_checkjoblevelup(struct map_session_data *sd)
{
    int next = pc_nextjobexp(sd);

    nullpo_ret(sd);

    if (sd->status.job_exp >= next && next > 0)
    {
        if (pc_skillpt_potential(sd) < sd->status.skill_point)
        {                       // [Fate] Bah, this is is painful.
            // But the alternative is quite error-prone, and eAthena has far worse performance issues...
            sd->status.job_exp = next - 1;
            pc_calcstatus(sd,0);
            return 0;
        }

        // job側レベルアップ処理
        sd->status.job_exp -= next;
        clif_updatestatus(sd, SP_NEXTJOBEXP);
        sd->status.skill_point++;
        clif_updatestatus(sd, SP_SKILLPOINT);
        pc_calcstatus(sd, 0);

        MAP_LOG_PC(sd, "SKILLPOINTS-UP %d", sd->status.skill_point);

        if (sd->status.job_level < 250
            && sd->status.job_level < sd->status.base_level * 2)
            sd->status.job_level++; // Make levelling up a little harder

        clif_misceffect(&sd->bl, 1);
        return 1;
    }

    return 0;
}

/*==========================================
 * 経験値取得
 *------------------------------------------
 */
int pc_gainexp(struct map_session_data *sd, int base_exp, int job_exp)
{
    return pc_gainexp_reason(sd, base_exp, job_exp,
                              PC_GAINEXP_REASON_KILLING);
}

int pc_gainexp_reason(struct map_session_data *sd, int base_exp, int job_exp,
        PC_GAINEXP_REASON reason)
{
    nullpo_ret(sd);

    if (sd->bl.prev == NULL || pc_isdead(sd))
        return 0;

    if ((battle_config.pvp_exp == 0) && map[sd->bl.m].flag.pvp) // [MouseJstr]
        return 0;               // no exp on pvp maps

    earray<const char *, PC_GAINEXP_REASON, PC_GAINEXP_REASON::COUNT> reasons //=
    {{
        "KILLXP",
        "HEALXP",
        "SCRIPTXP",
    }};
    MAP_LOG_PC(sd, "GAINXP %d %d %s", base_exp, job_exp, reasons[reason]);

    if (sd->sc_data[SC_RICHMANKIM].timer != -1)
    {
        // added bounds checking [Vaalris]
        base_exp +=
            base_exp * (25 + sd->sc_data[SC_RICHMANKIM].val1 * 25) / 100;
        job_exp +=
            job_exp * (25 + sd->sc_data[SC_RICHMANKIM].val1 * 25) / 100;
    }

    if (!battle_config.multi_level_up && pc_nextbaseafter(sd))
    {
        while (sd->status.base_exp + base_exp >= pc_nextbaseafter(sd)
               && sd->status.base_exp <= pc_nextbaseexp(sd)
               && pc_nextbaseafter(sd) > 0)
        {
            base_exp *= .90;
        }
    }

    sd->status.base_exp += base_exp;

    // [Fate] Adjust experience points that healers can extract from this character
    if (reason != PC_GAINEXP_REASON_HEALING)
    {
        const int max_heal_xp =
            20 + (sd->status.base_level * sd->status.base_level);

        sd->heal_xp += base_exp;
        if (sd->heal_xp > max_heal_xp)
            sd->heal_xp = max_heal_xp;
    }

    if (sd->status.base_exp < 0)
        sd->status.base_exp = 0;

    while (pc_checkbaselevelup(sd));

    clif_updatestatus(sd, SP_BASEEXP);
    if (!battle_config.multi_level_up && pc_nextjobafter(sd))
    {
        while (sd->status.job_exp + job_exp >= pc_nextjobafter(sd)
               && sd->status.job_exp <= pc_nextjobexp(sd)
               && pc_nextjobafter(sd) > 0)
        {
            job_exp *= .90;
        }
    }

    sd->status.job_exp += job_exp;
    if (sd->status.job_exp < 0)
        sd->status.job_exp = 0;

    while (pc_checkjoblevelup(sd));

    clif_updatestatus(sd, SP_JOBEXP);

    if (battle_config.disp_experience)
    {
        std::string output = STRPRINTF(
                "Experienced Gained Base:%d Job:%d",
                base_exp, job_exp);
        clif_displaymessage(sd->fd, output);
    }

    return 0;
}

int pc_extract_healer_exp(struct map_session_data *sd, int max)
{
    int amount;
    nullpo_ret(sd);

    amount = sd->heal_xp;
    if (max < amount)
        amount = max;

    sd->heal_xp -= amount;
    return amount;
}

/*==========================================
 * base level側必要経験値計算
 *------------------------------------------
 */
int pc_nextbaseexp(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->status.base_level >= MAX_LEVEL || sd->status.base_level <= 0)
        return 0;

    return exp_table_0[sd->status.base_level - 1];
}

/*==========================================
 * job level側必要経験値計算
 *------------------------------------------
 */
int pc_nextjobexp(struct map_session_data *sd)
{
    // [fate]  For normal levels, this ranges from 20k to 50k, depending on job level.
    // Job level is at most twice the player's experience level (base_level).  Levelling
    // from 2 to 9 is 44 points, i.e., 880k to 2.2M job experience points (this is per
    // skill, obviously.)

    return 20000 + sd->status.job_level * 150;
}

/*==========================================
 * base level after next [Valaris]
 *------------------------------------------
 */
int pc_nextbaseafter(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->status.base_level >= MAX_LEVEL || sd->status.base_level <= 0)
        return 0;

    return exp_table_0[sd->status.base_level];
}

/*==========================================
 * job level after next [Valaris]
 *------------------------------------------
 */
int pc_nextjobafter(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->status.job_level >= MAX_LEVEL || sd->status.job_level <= 0)
        return 0;

    return exp_table_7[sd->status.job_level];
}

/*==========================================
 * 必要ステータスポイント計算
 *------------------------------------------
 */
// TODO: replace SP by ATTR
int pc_need_status_point(struct map_session_data *sd, SP type)
{
    int val;

    nullpo_retr(-1, sd);

    if (type < SP_STR || type > SP_LUK)
        return -1;
    val = sd->status.attrs[sp_to_attr(type)];

    return (val + 9) / 10 + 1;
}

/*==========================================
 * 能力値成長
 *------------------------------------------
 */
int pc_statusup(struct map_session_data *sd, SP type)
{
    int need, val = 0;

    nullpo_ret(sd);

    if (SP_STR <= type && type <= SP_LUK)
        val = sd->status.attrs[sp_to_attr(type)];

    need = pc_need_status_point(sd, type);
    if (type < SP_STR || type > SP_LUK || need < 0
        || need > sd->status.status_point
        || val >= battle_config.max_parameter)
    {
        clif_statusupack(sd, type, 0, val);
        clif_updatestatus(sd, SP_STATUSPOINT);
        return 1;
    }
    val = ++sd->status.attrs[sp_to_attr(type)];
    sd->status.status_point -= need;
    if (need != pc_need_status_point(sd, type))
    {
        clif_updatestatus(sd, sp_to_usp(type));
    }
    clif_updatestatus(sd, SP_STATUSPOINT);
    clif_updatestatus(sd, type);
    pc_calcstatus(sd, 0);
    clif_statusupack(sd, type, 1, val);

    MAP_LOG_STATS(sd, "STATUP");

    return 0;
}

/*==========================================
 * 能力値成長
 *------------------------------------------
 */
int pc_statusup2(struct map_session_data *sd, SP type, int val)
{
    nullpo_ret(sd);

    if (type < SP_STR || type > SP_LUK)
    {
        clif_statusupack(sd, type, 0, 0);
        return 1;
    }
    ATTR attr = sp_to_attr(type);
    val = sd->status.attrs[attr] + val;
    val = std::min(val, battle_config.max_parameter);
    val = std::max(val, 1);
    clif_updatestatus(sd, sp_to_usp(type));
    clif_updatestatus(sd, type);
    pc_calcstatus(sd, 0);
    clif_statusupack(sd, type, 1, val);
    MAP_LOG_STATS(sd, "STATUP2");

    return 0;
}

/*==========================================
 * スキルポイント割り振り
 *------------------------------------------
 */
int pc_skillup(struct map_session_data *sd, SkillID skill_num)
{
    nullpo_ret(sd);

    if (sd->status.skill[skill_num].id != SkillID::ZERO
        && sd->status.skill_point >= sd->status.skill[skill_num].lv
        && sd->status.skill[skill_num].lv < skill_db[skill_num].max_raise)
    {
        sd->status.skill_point -= sd->status.skill[skill_num].lv;
        sd->status.skill[skill_num].lv++;

        pc_calcstatus(sd, 0);
        clif_skillup(sd, skill_num);
        clif_updatestatus(sd, SP_SKILLPOINT);
        clif_skillinfoblock(sd);
        MAP_LOG_PC(sd, "SKILLUP %d %d %d",
                   uint16_t(skill_num), sd->status.skill[skill_num].lv, skill_power(sd, skill_num));
    }

    return 0;
}

/*==========================================
 * /resetlvl
 *------------------------------------------
 */
int pc_resetlvl(struct map_session_data *sd, int type)
{
    nullpo_ret(sd);

    for (SkillID i : erange(SkillID(1), MAX_SKILL))
    {
        sd->status.skill[i].lv = 0;
    }

    if (type == 1)
    {
        sd->status.skill_point = 0;
        sd->status.base_level = 1;
        sd->status.job_level = 1;
        sd->status.base_exp = 0;
        sd->status.job_exp = 0;
        sd->status.option = Option::ZERO;

        for (ATTR attr : ATTRs)
            sd->status.attrs[attr] = 1;
    }

    if (type == 2)
    {
        sd->status.skill_point = 0;
        sd->status.base_level = 1;
        sd->status.job_level = 1;
        sd->status.base_exp = 0;
        sd->status.job_exp = 0;
    }
    if (type == 3)
    {
        sd->status.base_level = 1;
        sd->status.base_exp = 0;
    }
    if (type == 4)
    {
        sd->status.job_level = 1;
        sd->status.job_exp = 0;
    }

    clif_updatestatus(sd, SP_STATUSPOINT);
    clif_updatestatus(sd, SP_STR);
    clif_updatestatus(sd, SP_AGI);
    clif_updatestatus(sd, SP_VIT);
    clif_updatestatus(sd, SP_INT);
    clif_updatestatus(sd, SP_DEX);
    clif_updatestatus(sd, SP_LUK);
    clif_updatestatus(sd, SP_BASELEVEL);
    clif_updatestatus(sd, SP_JOBLEVEL);
    clif_updatestatus(sd, SP_STATUSPOINT);
    clif_updatestatus(sd, SP_NEXTBASEEXP);
    clif_updatestatus(sd, SP_NEXTJOBEXP);
    clif_updatestatus(sd, SP_SKILLPOINT);

    clif_updatestatus(sd, SP_USTR);    // Updates needed stat points - Valaris
    clif_updatestatus(sd, SP_UAGI);
    clif_updatestatus(sd, SP_UVIT);
    clif_updatestatus(sd, SP_UINT);
    clif_updatestatus(sd, SP_UDEX);
    clif_updatestatus(sd, SP_ULUK);    // End Addition

    for (EQUIP i : EQUIPs)
    {
        // unequip items that can't be equipped by base 1 [Valaris]
        if (sd->equip_index[i] >= 0)
            if (!pc_isequip(sd, sd->equip_index[i]))
            {
                pc_unequipitem(sd, sd->equip_index[i], CalcStatus::LATER);
                sd->equip_index[i] = -1;
            }
    }

    clif_skillinfoblock(sd);
    pc_calcstatus(sd, 0);

    MAP_LOG_STATS(sd, "STATRESET");

    return 0;
}

/*==========================================
 * /resetstate
 *------------------------------------------
 */
int pc_resetstate(struct map_session_data *sd)
{

    nullpo_ret(sd);

    sd->status.status_point = stat_p[sd->status.base_level - 1];

    clif_updatestatus(sd, SP_STATUSPOINT);

    for (ATTR attr : ATTRs)
        sd->status.attrs[attr] = 1;
    for (ATTR attr : ATTRs)
        clif_updatestatus(sd, attr_to_sp(attr));
    for (ATTR attr : ATTRs)
        clif_updatestatus(sd, attr_to_usp(attr));

    pc_calcstatus(sd, 0);

    return 0;
}

/*==========================================
 * /resetskill
 *------------------------------------------
 */
int pc_resetskill(struct map_session_data *sd)
{
    int skill;

    nullpo_ret(sd);

    sd->status.skill_point += pc_calc_skillpoint(sd);

    for (SkillID i : erange(SkillID(1), MAX_SKILL))
        if ((skill = pc_checkskill(sd, i)) > 0)
        {
            sd->status.skill[i].lv = 0;
            sd->status.skill[i].flags = SkillFlags::ZERO;
        }

    clif_updatestatus(sd, SP_SKILLPOINT);
    clif_skillinfoblock(sd);
    pc_calcstatus(sd, 0);

    return 0;
}

/*==========================================
 * pcにダメージを与える
 *------------------------------------------
 */
int pc_damage(struct block_list *src, struct map_session_data *sd,
               int damage)
{
    int i = 0, j = 0;

    nullpo_ret(sd);

    // 既に死んでいたら無効
    if (pc_isdead(sd))
        return 0;
    // 座ってたら立ち上がる
    if (pc_issit(sd))
    {
        pc_setstand(sd);
        skill_gangsterparadise(sd, 0);
    }

    if (src)
    {
        if (src->type == BL_PC)
        {
            MAP_LOG_PC(sd, "INJURED-BY PC%d FOR %d",
                        ((struct map_session_data *) src)->status.char_id,
                        damage);
        }
        else
        {
            MAP_LOG_PC(sd, "INJURED-BY MOB%d FOR %d", src->id, damage);
        }
    }
    else
        MAP_LOG_PC(sd, "INJURED-BY null FOR %d", damage);

    // 歩 いていたら足を止める
    if (sd->sc_data[SC_ENDURE].timer == -1
        && !sd->special_state.infinite_endure)
        pc_stop_walking(sd, 3);
    // 演奏/ダンスの中断
    if (damage > sd->status.max_hp >> 2)
        skill_stop_dancing(&sd->bl, 0);

    sd->status.hp -= damage;

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)
        skill_status_change_end(&sd->bl, SC_TRICKDEAD, -1);
    if (bool(sd->status.option & Option::HIDE2))
        skill_status_change_end(&sd->bl, SC_HIDING, -1);
    if (bool(sd->status.option & Option::CLOAK))
        skill_status_change_end(&sd->bl, SC_CLOAKING, -1);
    if (bool(sd->status.option & Option::CHASEWALK))
        skill_status_change_end(&sd->bl, SC_CHASEWALK, -1);

    if (sd->status.hp > 0)
    {
        // まだ生きているならHP更新
        clif_updatestatus(sd, SP_HP);

        sd->canlog_tick = gettick();

        if (sd->status.party_id > 0)
        {                       // on-the-fly party hp updates [Valaris]
            struct party *p = party_search(sd->status.party_id);
            if (p != NULL)
                clif_party_hp(p, sd);
        }                       // end addition [Valaris]

        return 0;
    }

    MAP_LOG_PC(sd, "DEAD%s", "");

    // Character is dead!

    sd->status.hp = 0;
    // [Fate] Stop quickregen
    sd->quick_regeneration_hp.amount = 0;
    sd->quick_regeneration_sp.amount = 0;
    skill_update_heal_animation(sd);

    pc_setdead(sd);

    pc_stop_walking(sd, 0);
    skill_castcancel(&sd->bl, 0);  // 詠唱の中止
    clif_clearchar_area(&sd->bl, 1);
    skill_unit_out_all(&sd->bl, gettick(), 1);
    pc_setglobalreg(sd, "PC_DIE_COUNTER", ++sd->die_counter);  //死にカウンター書き込み
    skill_status_change_clear(&sd->bl, 0); // ステータス異常を解除する
    clif_updatestatus(sd, SP_HP);
    pc_calcstatus(sd, 0);
    // [Fate] Reset magic
    sd->cast_tick = gettick();
    magic_stop_completely(sd);

    for (i = 0; i < 5; i++)
        if (sd->dev.val1[i])
        {
            skill_status_change_end(&map_id2sd(sd->dev.val1[i])->bl,
                                     SC_DEVOTION, -1);
            sd->dev.val1[i] = sd->dev.val2[i] = 0;
        }

    if (battle_config.death_penalty_type > 0 && sd->status.base_level >= 20)
    {                           // changed penalty options, added death by player if pk_mode [Valaris]
        if (!map[sd->bl.m].flag.nopenalty)
        {
            if (battle_config.death_penalty_type == 1
                && battle_config.death_penalty_base > 0)
                sd->status.base_exp -=
                    (double) pc_nextbaseexp(sd) *
                    (double) battle_config.death_penalty_base / 10000;
            if (battle_config.pk_mode && src && src->type == BL_PC)
                sd->status.base_exp -=
                    (double) pc_nextbaseexp(sd) *
                    (double) battle_config.death_penalty_base / 10000;
            else if (battle_config.death_penalty_type == 2
                     && battle_config.death_penalty_base > 0)
            {
                if (pc_nextbaseexp(sd) > 0)
                    sd->status.base_exp -=
                        (double) sd->status.base_exp *
                        (double) battle_config.death_penalty_base / 10000;
                if (battle_config.pk_mode && src && src->type == BL_PC)
                    sd->status.base_exp -=
                        (double) sd->status.base_exp *
                        (double) battle_config.death_penalty_base / 10000;
            }
            if (sd->status.base_exp < 0)
                sd->status.base_exp = 0;
            clif_updatestatus(sd, SP_BASEEXP);

            if (battle_config.death_penalty_type == 1
                && battle_config.death_penalty_job > 0)
                sd->status.job_exp -=
                    (double) pc_nextjobexp(sd) *
                    (double) battle_config.death_penalty_job / 10000;
            if (battle_config.pk_mode && src && src->type == BL_PC)
                sd->status.job_exp -=
                    (double) pc_nextjobexp(sd) *
                    (double) battle_config.death_penalty_job / 10000;
            else if (battle_config.death_penalty_type == 2
                     && battle_config.death_penalty_job > 0)
            {
                if (pc_nextjobexp(sd) > 0)
                    sd->status.job_exp -=
                        (double) sd->status.job_exp *
                        (double) battle_config.death_penalty_job / 10000;
                if (battle_config.pk_mode && src && src->type == BL_PC)
                    sd->status.job_exp -=
                        (double) sd->status.job_exp *
                        (double) battle_config.death_penalty_job / 10000;
            }
            if (sd->status.job_exp < 0)
                sd->status.job_exp = 0;
            clif_updatestatus(sd, SP_JOBEXP);
        }
    }
    //ナイトメアモードアイテムドロップ
    if (map[sd->bl.m].flag.pvp_nightmaredrop)
    {                           // Moved this outside so it works when PVP isnt enabled and during pk mode [Ancyker]
        for (j = 0; j < MAX_DROP_PER_MAP; j++)
        {
            int id = map[sd->bl.m].drop_list[j].drop_id;
            int type = map[sd->bl.m].drop_list[j].drop_type;
            int per = map[sd->bl.m].drop_list[j].drop_per;
            if (id == 0)
                continue;
            if (id == -1)
            {                   //ランダムドロップ
                int eq_num = 0, eq_n[MAX_INVENTORY];
                memset(eq_n, 0, sizeof(eq_n));
                //先ず装備しているアイテム数をカウント
                for (i = 0; i < MAX_INVENTORY; i++)
                {
                    int k;
                    if ((type == 1 && !bool(sd->status.inventory[i].equip))
                        || (type == 2 && bool(sd->status.inventory[i].equip))
                        || type == 3)
                    {
                        //InventoryIndexを格納
                        for (k = 0; k < MAX_INVENTORY; k++)
                        {
                            if (eq_n[k] <= 0)
                            {
                                eq_n[k] = i;
                                break;
                            }
                        }
                        eq_num++;
                    }
                }
                if (eq_num > 0)
                {
                    int n = eq_n[MRAND(eq_num)];  //該当アイテムの中からランダム
                    if (MRAND(10000) < per)
                    {
                        if (bool(sd->status.inventory[n].equip))
                            pc_unequipitem(sd, n, CalcStatus::NOW);
                        pc_dropitem(sd, n, 1);
                    }
                }
            }
            else if (id > 0)
            {
                for (i = 0; i < MAX_INVENTORY; i++)
                {
                    if (sd->status.inventory[i].nameid == id    //ItemIDが一致していて
                        && MRAND(10000) < per  //ドロップ率判定もOKで
                        && ((type == 1 && !bool(sd->status.inventory[i].equip))   //タイプ判定もOKならドロップ
                            || (type == 2 && bool(sd->status.inventory[i].equip))
                            || type == 3))
                    {
                        if (bool(sd->status.inventory[i].equip))
                            pc_unequipitem(sd, i, CalcStatus::NOW);
                        pc_dropitem(sd, i, 1);
                        break;
                    }
                }
            }
        }
    }
    // pvp
    if (map[sd->bl.m].flag.pvp && !battle_config.pk_mode)
    {                           // disable certain pvp functions on pk_mode [Valaris]
        //ランキング計算
        if (!map[sd->bl.m].flag.pvp_nocalcrank)
        {
            sd->pvp_point -= 5;
            if (src && src->type == BL_PC)
                ((struct map_session_data *) src)->pvp_point++;
            //} //fixed wrong '{' placement by Lupus
            pc_setdead(sd);
        }
        // 強制送還
        if (sd->pvp_point < 0)
        {
            sd->pvp_point = 0;
            pc_setstand(sd);
            pc_setrestartvalue(sd, 3);
            pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x,
                       sd->status.save_point.y, 0);
        }
    }

    if (src && src->type == BL_PC)
    {
        // [Fate] PK death, trigger scripts
        argrec_t arg[3];
        arg[0].name = "@killerrid";
        arg[0].v.i = src->id;
        arg[1].name = "@victimrid";
        arg[1].v.i = sd->bl.id;
        arg[2].name = "@victimlvl";
        arg[2].v.i = sd->status.base_level;
        npc_event_doall_l("OnPCKilledEvent", sd->bl.id, 3, arg);
        npc_event_doall_l("OnPCKillEvent", src->id, 3, arg);
    }
    npc_event_doall_l("OnPCDieEvent", sd->bl.id, 0, NULL);

    return 0;
}

//
// script関 連
//
/*==========================================
 * script用PCステータス読み出し
 *------------------------------------------
 */
int pc_readparam(struct map_session_data *sd, SP type)
{
    int val = 0;

    nullpo_ret(sd);

    switch (type)
    {
        case SP_SKILLPOINT:
            val = sd->status.skill_point;
            break;
        case SP_STATUSPOINT:
            val = sd->status.status_point;
            break;
        case SP_ZENY:
            val = sd->status.zeny;
            break;
        case SP_BASELEVEL:
            val = sd->status.base_level;
            break;
        case SP_JOBLEVEL:
            val = sd->status.job_level;
            break;
        case SP_CLASS:
            val = sd->status.species;
            break;
        case SP_SEX:
            val = sd->sex;
            break;
        case SP_WEIGHT:
            val = sd->weight;
            break;
        case SP_MAXWEIGHT:
            val = sd->max_weight;
            break;
        case SP_BASEEXP:
            val = sd->status.base_exp;
            break;
        case SP_JOBEXP:
            val = sd->status.job_exp;
            break;
        case SP_NEXTBASEEXP:
            val = pc_nextbaseexp(sd);
            break;
        case SP_NEXTJOBEXP:
            val = pc_nextjobexp(sd);
            break;
        case SP_HP:
            val = sd->status.hp;
            break;
        case SP_MAXHP:
            val = sd->status.max_hp;
            break;
        case SP_SP:
            val = sd->status.sp;
            break;
        case SP_MAXSP:
            val = sd->status.max_sp;
            break;
        case SP_STR:
        case SP_AGI:
        case SP_VIT:
        case SP_INT:
        case SP_DEX:
        case SP_LUK:
            val = sd->status.attrs[sp_to_attr(type)];
            break;
        case SP_FAME:
            val = sd->fame;
            break;
    }

    return val;
}

/*==========================================
 * script用PCステータス設定
 *------------------------------------------
 */
int pc_setparam(struct map_session_data *sd, SP type, int val)
{
    int i = 0, up_level = 50;

    nullpo_ret(sd);

    switch (type)
    {
        case SP_BASELEVEL:
            if (val > sd->status.base_level)
            {
                for (i = 1; i <= (val - sd->status.base_level); i++)
                    sd->status.status_point +=
                        (sd->status.base_level + i + 14) / 4;
            }
            sd->status.base_level = val;
            sd->status.base_exp = 0;
            clif_updatestatus(sd, SP_BASELEVEL);
            clif_updatestatus(sd, SP_NEXTBASEEXP);
            clif_updatestatus(sd, SP_STATUSPOINT);
            clif_updatestatus(sd, SP_BASEEXP);
            pc_calcstatus(sd, 0);
            pc_heal(sd, sd->status.max_hp, sd->status.max_sp);
            break;
        case SP_JOBLEVEL:
            up_level -= 40;
            if (val >= sd->status.job_level)
            {
                if (val > up_level)
                    val = up_level;
                sd->status.skill_point += (val - sd->status.job_level);
                sd->status.job_level = val;
                sd->status.job_exp = 0;
                clif_updatestatus(sd, SP_JOBLEVEL);
                clif_updatestatus(sd, SP_NEXTJOBEXP);
                clif_updatestatus(sd, SP_JOBEXP);
                clif_updatestatus(sd, SP_SKILLPOINT);
                pc_calcstatus(sd, 0);
                clif_misceffect(&sd->bl, 1);
            }
            else
            {
                sd->status.job_level = val;
                sd->status.job_exp = 0;
                clif_updatestatus(sd, SP_JOBLEVEL);
                clif_updatestatus(sd, SP_NEXTJOBEXP);
                clif_updatestatus(sd, SP_JOBEXP);
                pc_calcstatus(sd, 0);
            }
            clif_updatestatus(sd, type);
            break;
        case SP_SKILLPOINT:
            sd->status.skill_point = val;
            break;
        case SP_STATUSPOINT:
            sd->status.status_point = val;
            break;
        case SP_ZENY:
            sd->status.zeny = val;
            break;
        case SP_BASEEXP:
            if (pc_nextbaseexp(sd) > 0)
            {
                sd->status.base_exp = val;
                if (sd->status.base_exp < 0)
                    sd->status.base_exp = 0;
                pc_checkbaselevelup(sd);
            }
            break;
        case SP_JOBEXP:
            if (pc_nextjobexp(sd) > 0)
            {
                sd->status.job_exp = val;
                if (sd->status.job_exp < 0)
                    sd->status.job_exp = 0;
                pc_checkjoblevelup(sd);
            }
            break;
        case SP_SEX:
            sd->sex = val;
            break;
        case SP_WEIGHT:
            sd->weight = val;
            break;
        case SP_MAXWEIGHT:
            sd->max_weight = val;
            break;
        case SP_HP:
            sd->status.hp = val;
            break;
        case SP_MAXHP:
            sd->status.max_hp = val;
            break;
        case SP_SP:
            sd->status.sp = val;
            break;
        case SP_MAXSP:
            sd->status.max_sp = val;
            break;
        case SP_STR:
        case SP_AGI:
        case SP_VIT:
        case SP_INT:
        case SP_DEX:
        case SP_LUK:
            sd->status.attrs[sp_to_attr(type)] = val;
            break;
        case SP_FAME:
            sd->fame = val;
            break;
    }
    clif_updatestatus(sd, type);

    return 0;
}

/*==========================================
 * HP/SP回復
 *------------------------------------------
 */
int pc_heal(struct map_session_data *sd, int hp, int sp)
{
//  if(battle_config.battle_log)
//      PRINTF("heal %d %d\n",hp,sp);

    nullpo_ret(sd);

    if (pc_checkoverhp(sd))
    {
        if (hp > 0)
            hp = 0;
    }
    if (pc_checkoversp(sd))
    {
        if (sp > 0)
            sp = 0;
    }

    if (sd->sc_data[SC_BERSERK].timer != -1) //バーサーク中は回復させないらしい
        return 0;

    if (hp + sd->status.hp > sd->status.max_hp)
        hp = sd->status.max_hp - sd->status.hp;
    if (sp + sd->status.sp > sd->status.max_sp)
        sp = sd->status.max_sp - sd->status.sp;
    sd->status.hp += hp;
    if (sd->status.hp <= 0)
    {
        sd->status.hp = 0;
        pc_damage(NULL, sd, 1);
        hp = 0;
    }
    sd->status.sp += sp;
    if (sd->status.sp <= 0)
        sd->status.sp = 0;
    if (hp)
        clif_updatestatus(sd, SP_HP);
    if (sp)
        clif_updatestatus(sd, SP_SP);

    if (sd->status.party_id > 0)
    {                           // on-the-fly party hp updates [Valaris]
        struct party *p = party_search(sd->status.party_id);
        if (p != NULL)
            clif_party_hp(p, sd);
    }                           // end addition [Valaris]

    return hp + sp;
}

/*==========================================
 * HP/SP回復
 *------------------------------------------
 */
static
int pc_itemheal_effect(struct map_session_data *sd, int hp, int sp);

static
int                     // Compute how quickly we regenerate (less is faster) for that amount
pc_heal_quick_speed(int amount)
{
    if (amount >= 100)
    {
        if (amount >= 500)
            return 0;
        if (amount >= 250)
            return 1;
        return 2;
    }
    else
    {                           // < 100
        if (amount >= 50)
            return 3;
        if (amount >= 20)
            return 4;
        return 5;
    }
}

static
void pc_heal_quick_accumulate(int new_amount,
                          struct quick_regeneration *quick_regen, int max)
{
    int current_amount = quick_regen->amount;
    int current_speed = quick_regen->speed;
    int new_speed = pc_heal_quick_speed(new_amount);

    int average_speed = ((new_speed * new_amount) + (current_speed * current_amount)) / (current_amount + new_amount); // new_amount > 0, current_amount >= 0

    quick_regen->speed = average_speed;
    quick_regen->amount = min(current_amount + new_amount, max);

    quick_regen->tickdelay = min(quick_regen->speed, quick_regen->tickdelay);
}

int pc_itemheal(struct map_session_data *sd, int hp, int sp)
{
    /* defer healing */
    if (hp > 0)
    {
        pc_heal_quick_accumulate(hp,
                                  &sd->quick_regeneration_hp,
                                  sd->status.max_hp - sd->status.hp);
        hp = 0;
    }
    if (sp > 0)
    {
        pc_heal_quick_accumulate(sp,
                                  &sd->quick_regeneration_sp,
                                  sd->status.max_sp - sd->status.sp);

        sp = 0;
    }

    /* Hurt right away, if necessary */
    if (hp < 0 || sp < 0)
        pc_itemheal_effect(sd, hp, sp);

    return 0;
}

/* pc_itemheal_effect is invoked once every 0.5s whenever the pc
 * has health recovery queued up (cf. pc_natural_heal_sub).
 */
static
int pc_itemheal_effect(struct map_session_data *sd, int hp, int sp)
{
    nullpo_ret(sd);

    if (sd->sc_data[SC_GOSPEL].timer != -1)  //バーサーク中は回復させないらしい
        return 0;

    if (sd->state.potionpitcher_flag)
    {
        sd->potion_hp = hp;
        sd->potion_sp = sp;
        return 0;
    }

    if (pc_checkoverhp(sd))
    {
        if (hp > 0)
            hp = 0;
    }
    if (pc_checkoversp(sd))
    {
        if (sp > 0)
            sp = 0;
    }
    if (hp > 0)
    {
        int bonus = (sd->paramc[ATTR::VIT] << 1) + 100;
        hp = hp * bonus / 100;
    }
    if (sp > 0)
    {
        int bonus = (sd->paramc[ATTR::INT] << 1) + 100;
        sp = sp * bonus / 100;
    }
    if (hp + sd->status.hp > sd->status.max_hp)
        hp = sd->status.max_hp - sd->status.hp;
    if (sp + sd->status.sp > sd->status.max_sp)
        sp = sd->status.max_sp - sd->status.sp;
    sd->status.hp += hp;
    if (sd->status.hp <= 0)
    {
        sd->status.hp = 0;
        pc_damage(NULL, sd, 1);
        hp = 0;
    }
    sd->status.sp += sp;
    if (sd->status.sp <= 0)
        sd->status.sp = 0;
    if (hp)
        clif_updatestatus(sd, SP_HP);
    if (sp)
        clif_updatestatus(sd, SP_SP);

    return 0;
}

/*==========================================
 * HP/SP回復
 *------------------------------------------
 */
int pc_percentheal(struct map_session_data *sd, int hp, int sp)
{
    nullpo_ret(sd);

    if (sd->state.potionpitcher_flag)
    {
        sd->potion_per_hp = hp;
        sd->potion_per_sp = sp;
        return 0;
    }

    if (pc_checkoverhp(sd))
    {
        if (hp > 0)
            hp = 0;
    }
    if (pc_checkoversp(sd))
    {
        if (sp > 0)
            sp = 0;
    }
    if (hp)
    {
        if (hp >= 100)
        {
            sd->status.hp = sd->status.max_hp;
        }
        else if (hp <= -100)
        {
            sd->status.hp = 0;
            pc_damage(NULL, sd, 1);
        }
        else
        {
            sd->status.hp += sd->status.max_hp * hp / 100;
            if (sd->status.hp > sd->status.max_hp)
                sd->status.hp = sd->status.max_hp;
            if (sd->status.hp <= 0)
            {
                sd->status.hp = 0;
                pc_damage(NULL, sd, 1);
                hp = 0;
            }
        }
    }
    if (sp)
    {
        if (sp >= 100)
        {
            sd->status.sp = sd->status.max_sp;
        }
        else if (sp <= -100)
        {
            sd->status.sp = 0;
        }
        else
        {
            sd->status.sp += sd->status.max_sp * sp / 100;
            if (sd->status.sp > sd->status.max_sp)
                sd->status.sp = sd->status.max_sp;
            if (sd->status.sp < 0)
                sd->status.sp = 0;
        }
    }
    if (hp)
        clif_updatestatus(sd, SP_HP);
    if (sp)
        clif_updatestatus(sd, SP_SP);

    return 0;
}

/*==========================================
 * 見た目変更
 *------------------------------------------
 */
int pc_changelook(struct map_session_data *sd, LOOK type, int val)
{
    nullpo_ret(sd);

    switch (type)
    {
        case LOOK_HAIR:
            sd->status.hair = val;
            break;
        case LOOK_WEAPON:
            sd->status.weapon = val;
            break;
        case LOOK_HEAD_BOTTOM:
            sd->status.head_bottom = val;
            break;
        case LOOK_HEAD_TOP:
            sd->status.head_top = val;
            break;
        case LOOK_HEAD_MID:
            sd->status.head_mid = val;
            break;
        case LOOK_HAIR_COLOR:
            sd->status.hair_color = val;
            break;
        case LOOK_CLOTHES_COLOR:
            sd->status.clothes_color = val;
            break;
        case LOOK_SHIELD:
            sd->status.shield = val;
            break;
        case LOOK_SHOES:
            break;
    }
    clif_changelook(&sd->bl, type, val);

    return 0;
}

/*==========================================
 * 付属品(鷹,ペコ,カート)設定
 *------------------------------------------
 */
int pc_setoption(struct map_session_data *sd, Option type)
{
    nullpo_ret(sd);

    sd->status.option = type;
    clif_changeoption(&sd->bl);
    pc_calcstatus(sd, 0);

    return 0;
}

/*==========================================
 * script用変数の値を読む
 *------------------------------------------
 */
int pc_readreg(struct map_session_data *sd, int reg)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < sd->reg_num; i++)
        if (sd->reg[i].index == reg)
            return sd->reg[i].data;

    return 0;
}

/*==========================================
 * script用変数の値を設定
 *------------------------------------------
 */
int pc_setreg(struct map_session_data *sd, int reg, int val)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < sd->reg_num; i++)
    {
        if (sd->reg[i].index == reg)
        {
            sd->reg[i].data = val;
            return 0;
        }
    }
    sd->reg_num++;
    RECREATE(sd->reg, struct script_reg, sd->reg_num);
    sd->reg[i].index = reg;
    sd->reg[i].data = val;

    return 0;
}

/*==========================================
 * script用文字列変数の値を読む
 *------------------------------------------
 */
char *pc_readregstr(struct map_session_data *sd, int reg)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < sd->regstr_num; i++)
        if (sd->regstr[i].index == reg)
            return sd->regstr[i].data;

    return NULL;
}

/*==========================================
 * script用文字列変数の値を設定
 *------------------------------------------
 */
int pc_setregstr(struct map_session_data *sd, int reg, const char *str)
{
    int i;

    nullpo_ret(sd);

    if (strlen(str) + 1 > sizeof(sd->regstr[0].data))
    {
        PRINTF("pc_setregstr(): String too long!\n");
        return 0;
    }

    for (i = 0; i < sd->regstr_num; i++)
        if (sd->regstr[i].index == reg)
        {
            strcpy(sd->regstr[i].data, str);
            return 0;
        }
    sd->regstr_num++;
    RECREATE(sd->regstr, struct script_regstr, sd->regstr_num);
    sd->regstr[i].index = reg;
    strcpy(sd->regstr[i].data, str);

    return 0;
}

/*==========================================
 * script用グローバル変数の値を読む
 *------------------------------------------
 */
int pc_readglobalreg(struct map_session_data *sd, const char *reg)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < sd->status.global_reg_num; i++)
    {
        if (strcmp(sd->status.global_reg[i].str, reg) == 0)
            return sd->status.global_reg[i].value;
    }

    return 0;
}

/*==========================================
 * script用グローバル変数の値を設定
 *------------------------------------------
 */
int pc_setglobalreg(struct map_session_data *sd, const char *reg, int val)
{
    int i;

    nullpo_ret(sd);

    //PC_DIE_COUNTERがスクリプトなどで変更された時の処理
    if (strcmp(reg, "PC_DIE_COUNTER") == 0 && sd->die_counter != val)
    {
        sd->die_counter = val;
        pc_calcstatus(sd, 0);
    }
    if (val == 0)
    {
        for (i = 0; i < sd->status.global_reg_num; i++)
        {
            if (strcmp(sd->status.global_reg[i].str, reg) == 0)
            {
                sd->status.global_reg[i] =
                    sd->status.global_reg[sd->status.global_reg_num - 1];
                sd->status.global_reg_num--;
                break;
            }
        }
        return 0;
    }
    for (i = 0; i < sd->status.global_reg_num; i++)
    {
        if (strcmp(sd->status.global_reg[i].str, reg) == 0)
        {
            sd->status.global_reg[i].value = val;
            return 0;
        }
    }
    if (sd->status.global_reg_num < GLOBAL_REG_NUM)
    {
        strcpy(sd->status.global_reg[i].str, reg);
        sd->status.global_reg[i].value = val;
        sd->status.global_reg_num++;
        return 0;
    }
    if (battle_config.error_log)
        PRINTF("pc_setglobalreg : couldn't set %s (GLOBAL_REG_NUM = %d)\n",
                reg, GLOBAL_REG_NUM);

    return 1;
}

/*==========================================
 * script用アカウント変数の値を読む
 *------------------------------------------
 */
int pc_readaccountreg(struct map_session_data *sd, const char *reg)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < sd->status.account_reg_num; i++)
    {
        if (strcmp(sd->status.account_reg[i].str, reg) == 0)
            return sd->status.account_reg[i].value;
    }

    return 0;
}

/*==========================================
 * script用アカウント変数の値を設定
 *------------------------------------------
 */
int pc_setaccountreg(struct map_session_data *sd, const char *reg, int val)
{
    int i;

    nullpo_ret(sd);

    if (val == 0)
    {
        for (i = 0; i < sd->status.account_reg_num; i++)
        {
            if (strcmp(sd->status.account_reg[i].str, reg) == 0)
            {
                sd->status.account_reg[i] =
                    sd->status.account_reg[sd->status.account_reg_num - 1];
                sd->status.account_reg_num--;
                break;
            }
        }
        intif_saveaccountreg(sd);
        return 0;
    }
    for (i = 0; i < sd->status.account_reg_num; i++)
    {
        if (strcmp(sd->status.account_reg[i].str, reg) == 0)
        {
            sd->status.account_reg[i].value = val;
            intif_saveaccountreg(sd);
            return 0;
        }
    }
    if (sd->status.account_reg_num < ACCOUNT_REG_NUM)
    {
        strcpy(sd->status.account_reg[i].str, reg);
        sd->status.account_reg[i].value = val;
        sd->status.account_reg_num++;
        intif_saveaccountreg(sd);
        return 0;
    }
    if (battle_config.error_log)
        PRINTF("pc_setaccountreg : couldn't set %s (ACCOUNT_REG_NUM = %d)\n",
                reg, ACCOUNT_REG_NUM);

    return 1;
}

/*==========================================
 * script用アカウント変数2の値を読む
 *------------------------------------------
 */
int pc_readaccountreg2(struct map_session_data *sd, const char *reg)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < sd->status.account_reg2_num; i++)
    {
        if (strcmp(sd->status.account_reg2[i].str, reg) == 0)
            return sd->status.account_reg2[i].value;
    }

    return 0;
}

/*==========================================
 * script用アカウント変数2の値を設定
 *------------------------------------------
 */
int pc_setaccountreg2(struct map_session_data *sd, const char *reg, int val)
{
    int i;

    nullpo_retr(1, sd);

    if (val == 0)
    {
        for (i = 0; i < sd->status.account_reg2_num; i++)
        {
            if (strcmp(sd->status.account_reg2[i].str, reg) == 0)
            {
                sd->status.account_reg2[i] =
                    sd->status.account_reg2[sd->status.account_reg2_num - 1];
                sd->status.account_reg2_num--;
                break;
            }
        }
        chrif_saveaccountreg2(sd);
        return 0;
    }
    for (i = 0; i < sd->status.account_reg2_num; i++)
    {
        if (strcmp(sd->status.account_reg2[i].str, reg) == 0)
        {
            sd->status.account_reg2[i].value = val;
            chrif_saveaccountreg2(sd);
            return 0;
        }
    }
    if (sd->status.account_reg2_num < ACCOUNT_REG2_NUM)
    {
        strcpy(sd->status.account_reg2[i].str, reg);
        sd->status.account_reg2[i].value = val;
        sd->status.account_reg2_num++;
        chrif_saveaccountreg2(sd);
        return 0;
    }
    if (battle_config.error_log)
        PRINTF("pc_setaccountreg2 : couldn't set %s (ACCOUNT_REG2_NUM = %d)\n",
             reg, ACCOUNT_REG2_NUM);

    return 1;
}

/*==========================================
 * イベントタイマー処理
 *------------------------------------------
 */
static
void pc_eventtimer(timer_id tid, tick_t, custom_id_t id, custom_data_t data)
{
    struct map_session_data *sd = map_id2sd(id);
    int i;
    if (sd == NULL)
        return;

    for (i = 0; i < MAX_EVENTTIMER; i++)
    {
        if (sd->eventtimer[i] == tid)
        {
            sd->eventtimer[i] = -1;
            npc_event(sd, (const char *) data, 0);
            break;
        }
    }
    free((void *) data);
    if (i == MAX_EVENTTIMER)
    {
        if (battle_config.error_log)
            PRINTF("pc_eventtimer: no such event timer\n");
    }
}

/*==========================================
 * イベントタイマー追加
 *------------------------------------------
 */
int pc_addeventtimer(struct map_session_data *sd, int tick, const char *name)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] == -1)
            break;

    if (i < MAX_EVENTTIMER)
    {
        char *evname = (char *) calloc(24, 1);
        strncpy(evname, name, 24);
        evname[23] = '\0';
        sd->eventtimer[i] = add_timer(gettick() + tick,
                                       pc_eventtimer, sd->bl.id,
                                       (int) evname);
        return 1;
    }

    return 0;
}

/*==========================================
 * イベントタイマー削除
 *------------------------------------------
 */
int pc_deleventtimer(struct map_session_data *sd, const char *name)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] != -1 && strcmp((char
                                                *) (get_timer(sd->eventtimer
                                                               [i])->data),
                                               name) == 0)
        {
            delete_timer(sd->eventtimer[i], pc_eventtimer);
            sd->eventtimer[i] = -1;
            break;
        }

    return 0;
}

/*==========================================
 * イベントタイマーカウント値追加
 *------------------------------------------
 */
int pc_addeventtimercount(struct map_session_data *sd, const char *name,
                           int tick)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] != -1 && strcmp((char
                                                *) (get_timer(sd->eventtimer
                                                               [i])->data),
                                               name) == 0)
        {
            addtick_timer(sd->eventtimer[i], tick);
            break;
        }

    return 0;
}

/*==========================================
 * イベントタイマー全削除
 *------------------------------------------
 */
int pc_cleareventtimer(struct map_session_data *sd)
{
    int i;

    nullpo_ret(sd);

    for (i = 0; i < MAX_EVENTTIMER; i++)
        if (sd->eventtimer[i] != -1)
        {
            delete_timer(sd->eventtimer[i], pc_eventtimer);
            sd->eventtimer[i] = -1;
        }

    return 0;
}

//
// 装 備物
//
/*==========================================
 * アイテムを装備する
 *------------------------------------------
 */
static
int pc_signal_advanced_equipment_change(struct map_session_data *sd, int n)
{
    if (bool(sd->status.inventory[n].equip & EPOS::SHOES))
        clif_changelook(&sd->bl, LOOK_SHOES, 0);
    if (bool(sd->status.inventory[n].equip & EPOS::GLOVES))
        clif_changelook(&sd->bl, LOOK_GLOVES, 0);
    if (bool(sd->status.inventory[n].equip & EPOS::CAPE))
        clif_changelook(&sd->bl, LOOK_CAPE, 0);
    if (bool(sd->status.inventory[n].equip & EPOS::MISC1))
        clif_changelook(&sd->bl, LOOK_MISC1, 0);
    if (bool(sd->status.inventory[n].equip & EPOS::MISC2))
        clif_changelook(&sd->bl, LOOK_MISC2, 0);
    return 0;
}

int pc_equipitem(struct map_session_data *sd, int n, EPOS)
{
    int nameid, arrow, view;
    struct item_data *id;
    //ｿｽ]ｿｽｿｽｿｽｿｽｿｽ{ｿｽqｿｽﾌ場合ｿｽﾌ鯉ｿｽｿｽﾌ職ｿｽﾆゑｿｽｿｽZｿｽoｿｽｿｽｿｽｿｽ

    nullpo_ret(sd);

    if (n < 0 || n >= MAX_INVENTORY)
    {
        clif_equipitemack(sd, 0, EPOS::ZERO, 0);
        return 0;
    }

    nameid = sd->status.inventory[n].nameid;
    id = sd->inventory_data[n];
    EPOS pos = pc_equippoint(sd, n);

    if (battle_config.battle_log)
        PRINTF("equip %d (%d) %x:%x\n",
                nameid, n, id->equip, pos);
    if (!pc_isequip(sd, n) || pos == EPOS::ZERO || sd->status.inventory[n].broken == 1)
    {                           // [Valaris]
        clif_equipitemack(sd, n, EPOS::ZERO, 0);    // fail
        return 0;
    }

// -- moonsoul (if player is berserk then cannot equip)
//
    if (sd->sc_data[SC_BERSERK].timer != -1)
    {
        clif_equipitemack(sd, n, EPOS::ZERO, 0);    // fail
        return 0;
    }

    if (pos == (EPOS::MISC2 | EPOS::CAPE))
    {
        // アクセサリ用例外処理
        EPOS epor = EPOS::ZERO;
        if (sd->equip_index[EQUIP::MISC2] >= 0)
            epor |= sd->status.inventory[sd->equip_index[EQUIP::MISC2]].equip;
        if (sd->equip_index[EQUIP::CAPE] >= 0)
            epor |= sd->status.inventory[sd->equip_index[EQUIP::CAPE]].equip;
        epor &= (EPOS::MISC2 | EPOS::CAPE);
        pos = (epor == EPOS::CAPE ? EPOS::MISC2 : EPOS::CAPE);
    }

    // TODO: make this code do what it's supposed to do,
    // instead of what it does
    arrow = pc_search_inventory(sd, pc_checkequip(sd, EPOS::LEGS | EPOS::CAPE));    // Added by RoVeRT
    for (EQUIP i : EQUIPs)
    {
        if (bool(pos & equip_pos[i]))
        {
            if (sd->equip_index[i] >= 0)    //Slot taken, remove item from there.
                pc_unequipitem(sd, sd->equip_index[i], CalcStatus::LATER);
            sd->equip_index[i] = n;
        }
    }
    // 弓矢装備
    if (pos == EPOS::ARROW)
    {
        clif_arrowequip(sd, n);
        clif_arrow_fail(sd, 3);    // 3=矢が装備できました
    }
    else
    {
        /* Don't update re-equipping if we're using a spell */
        if (!(pos == EPOS::GLOVES && sd->attack_spell_override))
            clif_equipitemack(sd, n, pos, 1);
    }

    for (EQUIP i : EQUIPs)
    {
        if (bool(pos & equip_pos[i]))
            sd->equip_index[i] = n;
    }
    sd->status.inventory[n].equip = pos;

    if (sd->inventory_data[n])
    {
        view = sd->inventory_data[n]->look;
        if (view == 0)
            view = sd->inventory_data[n]->nameid;
    }
    else
    {
        view = 0;
    }

    if (bool(sd->status.inventory[n].equip & EPOS::WEAPON))
    {
        sd->weapontype1 = view;
        pc_calcweapontype(sd);
        pc_set_weapon_look(sd);
    }
    if (bool(sd->status.inventory[n].equip & EPOS::SHIELD))
    {
        if (sd->inventory_data[n])
        {
            if (sd->inventory_data[n]->type == ItemType::WEAPON)
            {
                sd->status.shield = 0;
                if (sd->status.inventory[n].equip == EPOS::SHIELD)
                    sd->weapontype2 = view;
            }
            else if (sd->inventory_data[n]->type == ItemType::ARMOR)
            {
                sd->status.shield = view;
                sd->weapontype2 = 0;
            }
        }
        else
            sd->status.shield = sd->weapontype2 = 0;
        pc_calcweapontype(sd);
        clif_changelook(&sd->bl, LOOK_SHIELD, sd->status.shield);
    }
    if (bool(sd->status.inventory[n].equip & EPOS::LEGS))
    {
        sd->status.head_bottom = view;
        clif_changelook(&sd->bl, LOOK_HEAD_BOTTOM, sd->status.head_bottom);
    }
    if (bool(sd->status.inventory[n].equip & EPOS::HAT))
    {
        sd->status.head_top = view;
        clif_changelook(&sd->bl, LOOK_HEAD_TOP, sd->status.head_top);
    }
    if (bool(sd->status.inventory[n].equip & EPOS::TORSO))
    {
        sd->status.head_mid = view;
        clif_changelook(&sd->bl, LOOK_HEAD_MID, sd->status.head_mid);
    }
    pc_signal_advanced_equipment_change(sd, n);

    pc_checkallowskill(sd);    // 装備品でスキルか解除されるかチェック
    if (itemdb_look(sd->status.inventory[n].nameid) == 11 && arrow)
    {                           // Added by RoVeRT
        clif_arrowequip(sd, arrow);
        sd->status.inventory[arrow].equip = EPOS::ARROW;
    }
    pc_calcstatus(sd, 0);

    if (sd->special_state.infinite_endure)
    {
        if (sd->sc_data[SC_ENDURE].timer == -1)
            skill_status_change_start(&sd->bl, SC_ENDURE, 10, 1, 0, 0, 0, 0);
    }
    else
    {
        if (sd->sc_data[SC_ENDURE].timer != -1 && sd->sc_data[SC_ENDURE].val2)
            skill_status_change_end(&sd->bl, SC_ENDURE, -1);
    }

    if (sd->sc_data[SC_SIGNUMCRUCIS].timer != -1
        && !battle_check_undead(7, sd->def_ele))
        skill_status_change_end(&sd->bl, SC_SIGNUMCRUCIS, -1);
    if (sd->sc_data[SC_DANCING].timer != -1
        && (sd->status.weapon != 13 && sd->status.weapon != 14))
        skill_stop_dancing(&sd->bl, 0);

    return 0;
}

/*==========================================
 * 装 備した物を外す
 *------------------------------------------
 */
int pc_unequipitem(struct map_session_data *sd, int n, CalcStatus type)
{
    nullpo_ret(sd);

// -- moonsoul  (if player is berserk then cannot unequip)
//
    if (sd->sc_data[SC_BERSERK].timer != -1)
    {
        clif_unequipitemack(sd, n, EPOS::ZERO, 0);
        return 0;
    }

    if (battle_config.battle_log)
        PRINTF("unequip %d %x:%x\n",
                n, pc_equippoint(sd, n),
                sd->status.inventory[n].equip);
    if (bool(sd->status.inventory[n].equip))
    {
        for (EQUIP i : EQUIPs)
        {
            if (bool(sd->status.inventory[n].equip & equip_pos[i]))
                sd->equip_index[i] = -1;
        }
        if (bool(sd->status.inventory[n].equip & EPOS::WEAPON))
        {
            sd->weapontype1 = 0;
            sd->status.weapon = sd->weapontype2;
            pc_calcweapontype(sd);
            pc_set_weapon_look(sd);
        }
        if (bool(sd->status.inventory[n].equip & EPOS::SHIELD))
        {
            sd->status.shield = sd->weapontype2 = 0;
            pc_calcweapontype(sd);
            clif_changelook(&sd->bl, LOOK_SHIELD, sd->status.shield);
        }
        if (bool(sd->status.inventory[n].equip & EPOS::LEGS))
        {
            sd->status.head_bottom = 0;
            clif_changelook(&sd->bl, LOOK_HEAD_BOTTOM,
                             sd->status.head_bottom);
        }
        if (bool(sd->status.inventory[n].equip & EPOS::HAT))
        {
            sd->status.head_top = 0;
            clif_changelook(&sd->bl, LOOK_HEAD_TOP, sd->status.head_top);
        }
        if (bool(sd->status.inventory[n].equip & EPOS::TORSO))
        {
            sd->status.head_mid = 0;
            clif_changelook(&sd->bl, LOOK_HEAD_MID, sd->status.head_mid);
        }
        pc_signal_advanced_equipment_change(sd, n);

        if (sd->sc_data[SC_BROKNWEAPON].timer != -1
            && bool(sd->status.inventory[n].equip & EPOS::WEAPON)
            && sd->status.inventory[n].broken == 1)
            skill_status_change_end(&sd->bl, SC_BROKNWEAPON, -1);

        clif_unequipitemack(sd, n, sd->status.inventory[n].equip, 1);
        sd->status.inventory[n].equip = EPOS::ZERO;
        if (type == CalcStatus::NOW)
            pc_checkallowskill(sd);
        if (sd->weapontype1 == 0 && sd->weapontype2 == 0)
            skill_encchant_eremental_end(&sd->bl, StatusChange::NEGATIVE1);
    }
    else
    {
        clif_unequipitemack(sd, n, EPOS::ZERO, 0);
    }
    if (type == CalcStatus::NOW)
    {
        pc_calcstatus(sd, 0);
        if (sd->sc_data[SC_SIGNUMCRUCIS].timer != -1
            && !battle_check_undead(7, sd->def_ele))
            skill_status_change_end(&sd->bl, SC_SIGNUMCRUCIS, -1);
    }

    return 0;
}

int pc_unequipinvyitem(struct map_session_data *sd, int n, CalcStatus type)
{
    nullpo_retr(1, sd);

    for (EQUIP i : EQUIPs)
    {
        if (equip_pos[i] != EPOS::ZERO
            && !bool(equip_pos[i] & EPOS::ARROW) // probably a bug
            && sd->equip_index[i] == n)
        {
            //Slot taken, remove item from there.
            pc_unequipitem(sd, sd->equip_index[i], type);
            sd->equip_index[i] = -1;
        }
    }

    return 0;
}

/*==========================================
 * アイテムのindex番号を詰めたり
 * 装 備品の装備可能チェックを行なう
 *------------------------------------------
 */
int pc_checkitem(struct map_session_data *sd)
{
    int i, j, k, id, calc_flag = 0;
    struct item_data *it = NULL;

    nullpo_ret(sd);

    // 所持品空き詰め
    for (i = j = 0; i < MAX_INVENTORY; i++)
    {
        if ((id = sd->status.inventory[i].nameid) == 0)
            continue;
        if (battle_config.item_check && !itemdb_available(id))
        {
            if (battle_config.error_log)
                PRINTF("illeagal item id %d in %d[%s] inventory.\n", id,
                        sd->bl.id, sd->status.name);
            pc_delitem(sd, i, sd->status.inventory[i].amount, 3);
            continue;
        }
        if (i > j)
        {
            memcpy(&sd->status.inventory[j], &sd->status.inventory[i],
                    sizeof(struct item));
            sd->inventory_data[j] = sd->inventory_data[i];
        }
        j++;
    }
    if (j < MAX_INVENTORY)
        memset(&sd->status.inventory[j], 0,
                sizeof(struct item) * (MAX_INVENTORY - j));
    for (k = j; k < MAX_INVENTORY; k++)
        sd->inventory_data[k] = NULL;

    // カート内空き詰め
    for (i = j = 0; i < MAX_CART; i++)
    {
        if ((id = sd->status.cart[i].nameid) == 0)
            continue;
        if (battle_config.item_check && !itemdb_available(id))
        {
            if (battle_config.error_log)
                PRINTF("illeagal item id %d in %d[%s] cart.\n", id,
                        sd->bl.id, sd->status.name);
            pc_cart_delitem(sd, i, sd->status.cart[i].amount, 1);
            continue;
        }
        if (i > j)
        {
            memcpy(&sd->status.cart[j], &sd->status.cart[i],
                    sizeof(struct item));
        }
        j++;
    }
    if (j < MAX_CART)
        memset(&sd->status.cart[j], 0,
                sizeof(struct item) * (MAX_CART - j));

    // 装 備位置チェック

    for (i = 0; i < MAX_INVENTORY; i++)
    {

        it = sd->inventory_data[i];

        if (sd->status.inventory[i].nameid == 0)
            continue;
        if (bool(sd->status.inventory[i].equip & ~pc_equippoint(sd, i)))
        {
            sd->status.inventory[i].equip = EPOS::ZERO;
            calc_flag = 1;
        }
        //装備制限チェック
        if (bool(sd->status.inventory[i].equip)
            && map[sd->bl.m].flag.pvp
            && (it->flag.no_equip == 1 || it->flag.no_equip == 3))
        {                       //PvP制限
            sd->status.inventory[i].equip = EPOS::ZERO;
            calc_flag = 1;
        }
    }

    pc_setequipindex(sd);
    if (calc_flag)
        pc_calcstatus(sd, 2);

    return 0;
}

int pc_checkoverhp(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->status.hp == sd->status.max_hp)
        return 1;
    if (sd->status.hp > sd->status.max_hp)
    {
        sd->status.hp = sd->status.max_hp;
        clif_updatestatus(sd, SP_HP);
        return 2;
    }

    return 0;
}

int pc_checkoversp(struct map_session_data *sd)
{
    nullpo_ret(sd);

    if (sd->status.sp == sd->status.max_sp)
        return 1;
    if (sd->status.sp > sd->status.max_sp)
    {
        sd->status.sp = sd->status.max_sp;
        clif_updatestatus(sd, SP_SP);
        return 2;
    }

    return 0;
}

/*==========================================
 * PVP順位計算用(foreachinarea)
 *------------------------------------------
 */
static
void pc_calc_pvprank_sub(struct block_list *bl, struct map_session_data *sd2)
{
    struct map_session_data *sd1;

    nullpo_retv(bl);
    sd1 = (struct map_session_data *) bl;
    nullpo_retv(sd2);

    if (sd1->pvp_point > sd2->pvp_point)
        sd2->pvp_rank++;
}

/*==========================================
 * PVP順位計算
 *------------------------------------------
 */
int pc_calc_pvprank(struct map_session_data *sd)
{
    struct map_data *m;

    nullpo_ret(sd);
    nullpo_ret(m = &map[sd->bl.m]);

    if (!(m->flag.pvp))
        return 0;
    sd->pvp_rank = 1;
    map_foreachinarea(std::bind(pc_calc_pvprank_sub, ph::_1, sd),
            sd->bl.m, 0, 0, m->xs, m->ys,
            BL_PC);
    return sd->pvp_rank;
}

/*==========================================
 * PVP順位計算(timer)
 *------------------------------------------
 */
void pc_calc_pvprank_timer(timer_id, tick_t, custom_id_t id, custom_data_t data)
{
    struct map_session_data *sd = NULL;
    if (battle_config.pk_mode)  // disable pvp ranking if pk_mode on [Valaris]
        return;

    sd = map_id2sd(id);
    if (sd == NULL)
        return;
    sd->pvp_timer = -1;
    if (pc_calc_pvprank(sd) > 0)
        sd->pvp_timer = add_timer(gettick() + PVP_CALCRANK_INTERVAL,
                                   pc_calc_pvprank_timer, id, data);
}

/*==========================================
 * sdは結婚しているか(既婚の場合は相方のchar_idを返す)
 *------------------------------------------
 */
static
int pc_ismarried(struct map_session_data *sd)
{
    if (sd == NULL)
        return -1;
    if (sd->status.partner_id > 0)
        return sd->status.partner_id;
    else
        return 0;
}

/*==========================================
 * sdがdstsdと結婚(dstsd→sdの結婚処理も同時に行う)
 *------------------------------------------
 */
int pc_marriage(struct map_session_data *sd, struct map_session_data *dstsd)
{
    if (sd == NULL || dstsd == NULL || sd->status.partner_id > 0
        || dstsd->status.partner_id > 0)
        return -1;
    sd->status.partner_id = dstsd->status.char_id;
    dstsd->status.partner_id = sd->status.char_id;
    return 0;
}

/*==========================================
 * sdが離婚(相手はsd->status.partner_idに依る)(相手も同時に離婚・結婚指輪自動剥奪)
 *------------------------------------------
 */
int pc_divorce(struct map_session_data *sd)
{
    struct map_session_data *p_sd = NULL;
    if (sd == NULL || !pc_ismarried(sd))
        return -1;

    // If both are on map server we don't need to bother the char server
    if ((p_sd =
         map_nick2sd(map_charid2nick(sd->status.partner_id))) != NULL)
    {
        if (p_sd->status.partner_id != sd->status.char_id
            || sd->status.partner_id != p_sd->status.char_id)
        {
            PRINTF("pc_divorce: Illegal partner_id sd=%d p_sd=%d\n",
                    sd->status.partner_id, p_sd->status.partner_id);
            return -1;
        }
        p_sd->status.partner_id = 0;
        sd->status.partner_id = 0;

        if (sd->npc_flags.divorce)
        {
            sd->npc_flags.divorce = 0;
            map_scriptcont(sd, sd->npc_id);
        }
    }
    else
        chrif_send_divorce(sd->status.char_id);

    return 0;
}

/*==========================================
 * sdの相方のmap_session_dataを返す
 *------------------------------------------
 */
struct map_session_data *pc_get_partner(struct map_session_data *sd)
{
    struct map_session_data *p_sd = NULL;
    char *nick;
    if (sd == NULL || !pc_ismarried(sd))
        return NULL;

    nick = map_charid2nick(sd->status.partner_id);

    if (nick == NULL)
        return NULL;

    if ((p_sd = map_nick2sd(nick)) == NULL)
        return NULL;

    return p_sd;
}

//
// 自然回復物
//
/*==========================================
 * SP回復量計算
 *------------------------------------------
 */
static
int natural_heal_tick, natural_heal_prev_tick, natural_heal_diff_tick;
static
int pc_spheal(struct map_session_data *sd)
{
    int a;

    nullpo_ret(sd);

    a = natural_heal_diff_tick;
    if (pc_issit(sd))
        a += a;
    if (sd->sc_data[SC_MAGNIFICAT].timer != -1) // マグニフィカート
        a += a;

    return a;
}

/*==========================================
 * HP回復量計算
 *------------------------------------------
 */
static
int pc_hpheal(struct map_session_data *sd)
{
    int a;

    nullpo_ret(sd);

    a = natural_heal_diff_tick;
    if (pc_issit(sd))
        a += a;
    if (sd->sc_data[SC_MAGNIFICAT].timer != -1) // Modified by RoVeRT
        a += a;

    return a;
}

static
int pc_natural_heal_hp(struct map_session_data *sd)
{
    int bhp;
    int inc_num, bonus, skill;

    nullpo_ret(sd);

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)  // Modified by RoVeRT
        return 0;

    if (pc_checkoverhp(sd))
    {
        sd->hp_sub = sd->inchealhptick = 0;
        return 0;
    }

    bhp = sd->status.hp;

    if (sd->walktimer == -1)
    {
        inc_num = pc_hpheal(sd);
        if (sd->sc_data[SC_TENSIONRELAX].timer != -1)
        {                       // テンションリラックス
            sd->hp_sub += 2 * inc_num;
            sd->inchealhptick += 3 * natural_heal_diff_tick;
        }
        else
        {
            sd->hp_sub += inc_num;
            sd->inchealhptick += natural_heal_diff_tick;
        }
    }
    else
    {
        sd->hp_sub = sd->inchealhptick = 0;
        return 0;
    }

    if (sd->hp_sub >= battle_config.natural_healhp_interval)
    {
        bonus = sd->nhealhp;
        while (sd->hp_sub >= battle_config.natural_healhp_interval)
        {
            sd->hp_sub -= battle_config.natural_healhp_interval;
            if (sd->status.hp + bonus <= sd->status.max_hp)
                sd->status.hp += bonus;
            else
            {
                sd->status.hp = sd->status.max_hp;
                sd->hp_sub = sd->inchealhptick = 0;
            }
        }
    }
    if (bhp != sd->status.hp)
        clif_updatestatus(sd, SP_HP);

    if (sd->nshealhp > 0)
    {
        if (sd->inchealhptick >= battle_config.natural_heal_skill_interval
            && sd->status.hp < sd->status.max_hp)
        {
            bonus = sd->nshealhp;
            while (sd->inchealhptick >=
                   battle_config.natural_heal_skill_interval)
            {
                sd->inchealhptick -=
                    battle_config.natural_heal_skill_interval;
                if (sd->status.hp + bonus <= sd->status.max_hp)
                    sd->status.hp += bonus;
                else
                {
                    bonus = sd->status.max_hp - sd->status.hp;
                    sd->status.hp = sd->status.max_hp;
                    sd->hp_sub = sd->inchealhptick = 0;
                }
            }
        }
    }
    else
        sd->inchealhptick = 0;

    return 0;

    if (sd->sc_data[SC_APPLEIDUN].timer != -1)
    {                           // Apple of Idun
        if (sd->inchealhptick >= 6000 && sd->status.hp < sd->status.max_hp)
        {
            bonus = skill * 20;
            while (sd->inchealhptick >= 6000)
            {
                sd->inchealhptick -= 6000;
                if (sd->status.hp + bonus <= sd->status.max_hp)
                    sd->status.hp += bonus;
                else
                {
                    bonus = sd->status.max_hp - sd->status.hp;
                    sd->status.hp = sd->status.max_hp;
                    sd->hp_sub = sd->inchealhptick = 0;
                }
            }
        }
    }
    else
        sd->inchealhptick = 0;

    return 0;
}

static
int pc_natural_heal_sp(struct map_session_data *sd)
{
    int bsp;
    int inc_num, bonus;

    nullpo_ret(sd);

    if (sd->sc_data[SC_TRICKDEAD].timer != -1)  // Modified by RoVeRT
        return 0;

    if (pc_checkoversp(sd))
    {
        sd->sp_sub = sd->inchealsptick = 0;
        return 0;
    }

    bsp = sd->status.sp;

    inc_num = pc_spheal(sd);
    if (sd->sc_data[SC_EXPLOSIONSPIRITS].timer == -1)
        sd->sp_sub += inc_num;
    if (sd->walktimer == -1)
        sd->inchealsptick += natural_heal_diff_tick;
    else
        sd->inchealsptick = 0;

    if (sd->sp_sub >= battle_config.natural_healsp_interval)
    {
        bonus = sd->nhealsp;;
        while (sd->sp_sub >= battle_config.natural_healsp_interval)
        {
            sd->sp_sub -= battle_config.natural_healsp_interval;
            if (sd->status.sp + bonus <= sd->status.max_sp)
                sd->status.sp += bonus;
            else
            {
                sd->status.sp = sd->status.max_sp;
                sd->sp_sub = sd->inchealsptick = 0;
            }
        }
    }

    if (bsp != sd->status.sp)
        clif_updatestatus(sd, SP_SP);

    if (sd->nshealsp > 0)
    {
        if (sd->inchealsptick >= battle_config.natural_heal_skill_interval
            && sd->status.sp < sd->status.max_sp)
        {
            bonus = sd->nshealsp;
            sd->doridori_counter = 0;
            while (sd->inchealsptick >=
                   battle_config.natural_heal_skill_interval)
            {
                sd->inchealsptick -=
                    battle_config.natural_heal_skill_interval;
                if (sd->status.sp + bonus <= sd->status.max_sp)
                    sd->status.sp += bonus;
                else
                {
                    bonus = sd->status.max_sp - sd->status.sp;
                    sd->status.sp = sd->status.max_sp;
                    sd->sp_sub = sd->inchealsptick = 0;
                }
            }
        }
    }
    else
        sd->inchealsptick = 0;

    return 0;
}

/*==========================================
 * HP/SP 自然回復 各クライアント
 *------------------------------------------
 */

static
int pc_quickregenerate_effect(struct quick_regeneration *quick_regen,
                           int heal_speed)
{
    if (!(quick_regen->tickdelay--))
    {
        int bonus =
            min(heal_speed * battle_config.itemheal_regeneration_factor,
                 quick_regen->amount);

        quick_regen->amount -= bonus;

        quick_regen->tickdelay = quick_regen->speed;

        return bonus;
    }

    return 0;
}

static
void pc_natural_heal_sub(struct map_session_data *sd)
{
    nullpo_retv(sd);

    if (sd->heal_xp > 0)
    {
        if (sd->heal_xp < 64)
            --sd->heal_xp;      // [Fate] Slowly reduce XP that healers can get for healing this char
        else
            sd->heal_xp -= (sd->heal_xp >> 6);
    }

    // Hijack this callback:  Adjust spellpower bonus
    if (sd->spellpower_bonus_target < sd->spellpower_bonus_current)
    {
        sd->spellpower_bonus_current = sd->spellpower_bonus_target;
        pc_calcstatus(sd, 0);
    }
    else if (sd->spellpower_bonus_target > sd->spellpower_bonus_current)
    {
        sd->spellpower_bonus_current +=
            1 +
            ((sd->spellpower_bonus_target -
              sd->spellpower_bonus_current) >> 5);
        pc_calcstatus(sd, 0);
    }

    if (sd->sc_data[SC_HALT_REGENERATE].timer != -1)
        return;

    if (sd->quick_regeneration_hp.amount || sd->quick_regeneration_sp.amount)
    {
        int hp_bonus = pc_quickregenerate_effect(&sd->quick_regeneration_hp,
                                                   (sd->sc_data[SC_POISON].timer == -1 || sd->sc_data[SC_SLOWPOISON].timer != -1) ? sd->nhealhp : 1);   // [fate] slow down when poisoned
        int sp_bonus = pc_quickregenerate_effect(&sd->quick_regeneration_sp,
                                                   sd->nhealsp);

        pc_itemheal_effect(sd, hp_bonus, sp_bonus);
    }
    skill_update_heal_animation(sd);   // if needed.

// -- moonsoul (if conditions below altered to disallow natural healing if under berserk status)
    if ((sd->sc_data[SC_FLYING_BACKPACK].timer != -1
         || battle_config.natural_heal_weight_rate > 100
         || sd->weight * 100 / sd->max_weight <
         battle_config.natural_heal_weight_rate) && !pc_isdead(sd)
        && !pc_ishiding(sd) && sd->sc_data[SC_POISON].timer == -1)
    {
        pc_natural_heal_hp(sd);
        if (sd->sc_data[SC_EXTREMITYFIST].timer == -1 && //阿修羅状態ではSPが回復しない
            sd->sc_data[SC_DANCING].timer == -1 &&  //ダンス状態ではSPが回復しない
            sd->sc_data[SC_BERSERK].timer == -1 //バーサーク状態ではSPが回復しない
            )
            pc_natural_heal_sp(sd);
    }
    else
    {
        sd->hp_sub = sd->inchealhptick = 0;
        sd->sp_sub = sd->inchealsptick = 0;
    }
    sd->inchealspirithptick = 0;
    sd->inchealspiritsptick = 0;
}

/*==========================================
 * HP/SP自然回復 (interval timer関数)
 *------------------------------------------
 */
static
void pc_natural_heal(timer_id, tick_t tick, custom_id_t, custom_data_t)
{
    natural_heal_tick = tick;
    natural_heal_diff_tick =
        DIFF_TICK(natural_heal_tick, natural_heal_prev_tick);
    clif_foreachclient(pc_natural_heal_sub);

    natural_heal_prev_tick = tick;
}

/*==========================================
 * セーブポイントの保存
 *------------------------------------------
 */
int pc_setsavepoint(struct map_session_data *sd, const char *mapname, int x, int y)
{
    nullpo_ret(sd);

    strncpy(sd->status.save_point.map, mapname, 23);
    sd->status.save_point.map[23] = '\0';
    sd->status.save_point.x = x;
    sd->status.save_point.y = y;

    return 0;
}

/*==========================================
 * 自動セーブ 各クライアント
 *------------------------------------------
 */
static
int last_save_fd, save_flag;
static
void pc_autosave_sub(struct map_session_data *sd)
{
    nullpo_retv(sd);

    if (save_flag == 0 && sd->fd > last_save_fd)
    {
        pc_makesavestatus(sd);
        chrif_save(sd);

        save_flag = 1;
        last_save_fd = sd->fd;
    }
}

/*==========================================
 * 自動セーブ (timer関数)
 *------------------------------------------
 */
static
void pc_autosave(timer_id, tick_t, custom_id_t, custom_data_t)
{
    int interval;

    save_flag = 0;
    clif_foreachclient(pc_autosave_sub);
    if (save_flag == 0)
        last_save_fd = 0;

    interval = autosave_interval / (clif_countusers() + 1);
    if (interval <= 0)
        interval = 1;
    add_timer(gettick() + interval, pc_autosave, 0, 0);
}

int pc_read_gm_account(int fd)
{
    int i = 0;
    if (gm_account != NULL)
        free(gm_account);
    GM_num = 0;

    CREATE(gm_account, struct gm_account, (RFIFOW(fd, 2) - 4) / 5);
    for (i = 4; i < RFIFOW(fd, 2); i = i + 5)
    {
        gm_account[GM_num].account_id = RFIFOL(fd, i);
        gm_account[GM_num].level = (int) RFIFOB(fd, i + 4);
        //PRINTF("GM account: %d -> level %d\n", gm_account[GM_num].account_id, gm_account[GM_num].level);
        GM_num++;
    }
    return GM_num;
}

/*==========================================
 * timer to do the day
 *------------------------------------------
 */
static
void map_day_timer(timer_id, tick_t, custom_id_t, custom_data_t)
{
    // by [yor]
    struct map_session_data *pl_sd = NULL;
    int i;
    char tmpstr[1024];

    if (battle_config.day_duration > 0)
    {                           // if we want a day
        if (night_flag != 0)
        {
            strcpy(tmpstr, "The day has arrived!");
            night_flag = 0;     // 0=day, 1=night [Yor]
            for (i = 0; i < fd_max; i++)
            {
                if (session[i] && (pl_sd = (struct map_session_data *)session[i]->session_data)
                    && pl_sd->state.auth)
                {
                    pl_sd->opt2 &= ~Opt2::BLIND;
                    clif_changeoption(&pl_sd->bl);
                    clif_wis_message(pl_sd->fd, wisp_server_name, tmpstr,
                                      strlen(tmpstr) + 1);
                }
            }
        }
    }
}

/*==========================================
 * timer to do the night
 *------------------------------------------
 */
static
void map_night_timer(timer_id, tick_t, custom_id_t, custom_data_t)
{
    // by [yor]
    struct map_session_data *pl_sd = NULL;
    int i;
    char tmpstr[1024];

    if (battle_config.night_duration > 0)
    {                           // if we want a night
        if (night_flag == 0)
        {
            strcpy(tmpstr, "The night has fallen...");
            night_flag = 1;     // 0=day, 1=night [Yor]
            for (i = 0; i < fd_max; i++)
            {
                if (session[i] && (pl_sd = (struct map_session_data *)session[i]->session_data)
                    && pl_sd->state.auth)
                {
                    pl_sd->opt2 |= Opt2::BLIND;
                    clif_changeoption(&pl_sd->bl);
                    clif_wis_message(pl_sd->fd, wisp_server_name, tmpstr,
                                      strlen(tmpstr) + 1);
                }
            }
        }
    }
}

void pc_setstand(struct map_session_data *sd)
{
    nullpo_retv(sd);

    if (sd->sc_data[SC_TENSIONRELAX].timer != -1)
        skill_status_change_end(&sd->bl, SC_TENSIONRELAX, -1);

    sd->state.dead_sit = 0;
}

static
int pc_calc_sigma(void)
{
    int j, k;

    {
        memset(hp_sigma_val_0, 0, sizeof(hp_sigma_val_0));
        for (k = 0, j = 2; j <= MAX_LEVEL; j++)
        {
            k += hp_coefficient_0 * j + 50;
            k -= k % 100;
            hp_sigma_val_0[j - 1] = k;
        }
    }
    return 0;
}

/*==========================================
 * pc関 係初期化
 *------------------------------------------
 */
int do_init_pc(void)
{
    pc_calc_sigma();

//  gm_account_db = numdb_init();

    add_timer_interval((natural_heal_prev_tick =
                         gettick() + NATURAL_HEAL_INTERVAL), pc_natural_heal,
                        0, 0, NATURAL_HEAL_INTERVAL);
    add_timer(gettick() + autosave_interval, pc_autosave, 0, 0);

    {
        int day_duration = battle_config.day_duration;
        int night_duration = battle_config.night_duration;
        if (day_duration < 60000)
            day_duration = 60000;
        if (night_duration < 60000)
            night_duration = 60000;
        if (battle_config.night_at_start == 0)
        {
            night_flag = 0;     // 0=day, 1=night [Yor]
            day_timer_tid =
                add_timer_interval(gettick() + day_duration +
                                    night_duration, map_day_timer, 0, 0,
                                    day_duration + night_duration);
            night_timer_tid =
                add_timer_interval(gettick() + day_duration,
                                    map_night_timer, 0, 0,
                                    day_duration + night_duration);
        }
        else
        {
            night_flag = 1;     // 0=day, 1=night [Yor]
            day_timer_tid =
                add_timer_interval(gettick() + night_duration,
                                    map_day_timer, 0, 0,
                                    day_duration + night_duration);
            night_timer_tid =
                add_timer_interval(gettick() + day_duration +
                                    night_duration, map_night_timer, 0, 0,
                                    day_duration + night_duration);
        }
    }

    return 0;
}

void pc_cleanup(struct map_session_data *sd)
{
    magic_stop_completely(sd);
}

void pc_invisibility(struct map_session_data *sd, int enabled)
{
    if (enabled && !bool(sd->status.option & Option::INVISIBILITY))
    {
        clif_clearchar_area(&sd->bl, 3);
        sd->status.option |= Option::INVISIBILITY;
        clif_status_change(&sd->bl, CLIF_OPTION_SC_INVISIBILITY, 1);
    }
    else if (!enabled)
    {
        sd->status.option &= ~Option::INVISIBILITY;
        clif_status_change(&sd->bl, CLIF_OPTION_SC_INVISIBILITY, 0);
        pc_setpos(sd, map[sd->bl.m].name, sd->bl.x, sd->bl.y, 3);
    }
}

int pc_logout(struct map_session_data *sd) // [fate] Player logs out
{
    if (!sd)
        return 0;

    if (sd->sc_data[SC_POISON].timer != -1)
        sd->status.hp = 1;      // Logging out while poisoned -> bad

    /*
     * Trying to rapidly sign out/in or switch characters to avoid a spell's
     * cast time is also bad. [remoitnane]
     */
#if 0
    // Removed because it's buggy, see above.
    if (sd->cast_tick > tick)
    {
        if (pc_setglobalreg(sd, "MAGIC_CAST_TICK", sd->cast_tick - tick))
            sd->status.sp = 1;
    }
    else
#endif
        pc_setglobalreg(sd, "MAGIC_CAST_TICK", 0);

    MAP_LOG_STATS(sd, "LOGOUT");
    return 0;
}
