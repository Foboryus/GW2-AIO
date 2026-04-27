#include "BadgeRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

// ============================================================================
// Badge Definitions
// ============================================================================

QList<BadgeDefinition> BadgeRegistry::allBadges() {
  static const QList<BadgeDefinition> badges = {
      // --- Basic badges (from /v2/account, no extra API calls) ---
      {"account_age", "Account Age", "badge-shield", "account", "account"},
      {"ap_total", "AP Total", "badge-star", "account", "account"},
      {"wvw_rank", "WvW Rank", "badge-swords", "account", "account"},
      {"fractal_level", "Fractal Level", "badge-diamond", "account", "account"},
      {"commander", "Commander", "badge-flag", "account", "account"},
      {"guild_count", "Guilds", "badge-guild", "account", "account"},
      {"world_id", "Home World", "badge-globe", "account", "account"},

      // --- Character badges (from /v2/characters) ---
      {"total_playtime", "Total Playtime", "badge-clock", "characters",
       "characters"},
      {"character_count", "Characters", "badge-users", "characters",
       "characters"},
      {"max_level_count", "Lv80 Characters", "badge-crown", "characters",
       "characters"},

      // --- Extended badges (require additional API endpoints) ---
      {"mastery_points", "Mastery Points", "badge-mastery",
       "account/mastery/points", "progression"},
      {"legendary_count", "Legendaries", "badge-legendary",
       "account/legendaryarmory", "unlocks"},
      {"mount_skins", "Mount Skins", "badge-mount", "account/mounts/skins",
       "unlocks"},
      {"dye_count", "Dyes", "badge-palette", "account/dyes", "unlocks"},
      {"pvp_rank", "PvP Rank", "badge-trophy", "pvp/stats", "pvp"},
  };
  return badges;
}

BadgeDefinition BadgeRegistry::badge(const QString &id) {
  for (const auto &b : allBadges()) {
    if (b.id == id)
      return b;
  }
  return {};
}

QStringList BadgeRegistry::requiredEndpoints(const QStringList &badgeIds) {
  QSet<QString> endpoints;
  for (const QString &id : badgeIds) {
    BadgeDefinition def = badge(id);
    if (!def.id.isEmpty()) {
      endpoints.insert(def.apiEndpoint);
    }
  }
  return endpoints.values();
}

// ============================================================================
// Value Formatting
// ============================================================================

QString BadgeRegistry::formatValue(const QString &badgeId,
                                   const QJsonDocument &data) {
  if (data.isNull())
    return QStringLiteral("\u2014"); // em-dash

  QJsonObject obj = data.object();

  // For array responses (legendaryarmory, mounts/skins, dyes, characters)
  QJsonArray arr = data.array();

  if (badgeId == "account_age") {
    // age is in seconds
    qint64 seconds = obj["age"].toInteger();
    if (seconds <= 0)
      return QStringLiteral("\u2014");
    int years = static_cast<int>(seconds / (365.25 * 24 * 3600));
    if (years > 0)
      return QString("%1 yr%2").arg(years).arg(years != 1 ? "s" : "");
    int days = static_cast<int>(seconds / (24 * 3600));
    return QString("%1 day%2").arg(days).arg(days != 1 ? "s" : "");
  }

  if (badgeId == "ap_total") {
    int daily = obj["daily_ap"].toInt();
    int monthly = obj["monthly_ap"].toInt();
    int total = daily + monthly;
    return QLocale().toString(total) + " AP";
  }

  if (badgeId == "wvw_rank") {
    int rank = obj["wvw_rank"].toInt();
    return QLocale().toString(rank);
  }

  if (badgeId == "fractal_level") {
    int level = obj["fractal_level"].toInt();
    return QString("Lv %1").arg(level);
  }

  if (badgeId == "commander") {
    bool isCommander = obj["commander"].toBool();
    return isCommander ? QStringLiteral("Yes") : QStringLiteral("No");
  }

  if (badgeId == "guild_count") {
    QJsonArray guilds = obj["guilds"].toArray();
    return QString::number(guilds.size());
  }

  if (badgeId == "world_id") {
    int wid = obj["world"].toInt();
    return worldName(wid);
  }

  if (badgeId == "total_playtime") {
    // Characters endpoint returns array of character objects
    qint64 totalSeconds = 0;
    for (const QJsonValue &val : arr) {
      totalSeconds += val.toObject()["age"].toInteger();
    }
    int hours = static_cast<int>(totalSeconds / 3600);
    return QLocale().toString(hours) + "h";
  }

  if (badgeId == "character_count") {
    return QString::number(arr.size());
  }

  if (badgeId == "max_level_count") {
    int count = 0;
    for (const QJsonValue &val : arr) {
      if (val.toObject()["level"].toInt() == 80)
        ++count;
    }
    return QString::number(count);
  }

  if (badgeId == "mastery_points") {
    // /v2/account/mastery/points returns {totals: [{region, spent, earned}]}
    QJsonArray totals = obj["totals"].toArray();
    int total = 0;
    for (const QJsonValue &val : totals) {
      total += val.toObject()["earned"].toInt();
    }
    return QLocale().toString(total) + " MP";
  }

  if (badgeId == "legendary_count") {
    // /v2/account/legendaryarmory returns array of {id, count}
    return QString::number(arr.size());
  }

  if (badgeId == "mount_skins") {
    return QString::number(arr.size());
  }

  if (badgeId == "dye_count") {
    // /v2/account/dyes returns array of dye IDs
    return QString::number(arr.size());
  }

  if (badgeId == "pvp_rank") {
    int rank = obj["pvp_rank"].toInt();
    return pvpRankTier(rank);
  }

  return QStringLiteral("\u2014");
}

// ============================================================================
// Static Lookup Tables
// ============================================================================

QString BadgeRegistry::worldName(int worldId) {
  // GW2 world IDs — NA and EU servers
  static const QMap<int, QString> worlds = {
      // NA
      {1001, "Anvil Rock"},
      {1002, "Borlis Pass"},
      {1003, "Yak's Bend"},
      {1004, "Henge of Denravi"},
      {1005, "Maguuma"},
      {1006, "Sorrow's Furnace"},
      {1007, "Gate of Madness"},
      {1008, "Jade Quarry"},
      {1009, "Fort Aspenwood"},
      {1010, "Ehmry Bay"},
      {1011, "Stormbluff Isle"},
      {1012, "Darkhaven"},
      {1013, "Sanctum of Rall"},
      {1014, "Crystal Desert"},
      {1015, "Isle of Janthir"},
      {1016, "Sea of Sorrows"},
      {1017, "Tarnished Coast"},
      {1018, "Northern Shiverpeaks"},
      {1019, "Blackgate"},
      {1020, "Ferguson's Crossing"},
      {1021, "Dragonbrand"},
      {1022, "Kaineng"},
      {1023, "Devona's Rest"},
      {1024, "Eredon Terrace"},
      // EU
      {2001, "Fissure of Woe"},
      {2002, "Desolation"},
      {2003, "Gandara"},
      {2004, "Blacktide"},
      {2005, "Ring of Fire"},
      {2006, "Underworld"},
      {2007, "Far Shiverpeaks"},
      {2008, "Whiteside Ridge"},
      {2009, "Ruins of Surmia"},
      {2010, "Seafarer's Rest"},
      {2011, "Vabbi"},
      {2012, "Piken Square"},
      {2013, "Aurora Glade"},
      {2014, "Gunnar's Hold"},
      {2101, "Jade Sea [FR]"},
      {2102, "Fort Ranik [FR]"},
      {2103, "Augury Rock [FR]"},
      {2104, "Vizunah Square [FR]"},
      {2105, "Arborstone [FR]"},
      {2201, "Kodash [DE]"},
      {2202, "Riverside [DE]"},
      {2203, "Elona Reach [DE]"},
      {2204, "Abaddon's Mouth [DE]"},
      {2205, "Drakkar Lake [DE]"},
      {2206, "Miller's Sound [DE]"},
      {2207, "Dzagonur [DE]"},
      {2301, "Baruch Bay [SP]"},
  };

  auto it = worlds.find(worldId);
  if (it != worlds.end())
    return it.value();
  return QString("World %1").arg(worldId);
}

QString BadgeRegistry::pvpRankTier(int rank) {
  // GW2 PvP rank tiers (each tier is 10 ranks)
  if (rank <= 0)
    return QStringLiteral("\u2014");
  if (rank <= 10)
    return QStringLiteral("Rabbit");
  if (rank <= 20)
    return QStringLiteral("Deer");
  if (rank <= 30)
    return QStringLiteral("Dolyak");
  if (rank <= 40)
    return QStringLiteral("Wolf");
  if (rank <= 50)
    return QStringLiteral("Tiger");
  if (rank <= 60)
    return QStringLiteral("Bear");
  if (rank <= 70)
    return QStringLiteral("Shark");
  if (rank <= 80)
    return QStringLiteral("Phoenix");
  return QStringLiteral("Dragon");
}
