#pragma once

// ========================================== //
// ===== GREGORIAN CALENDAR STRINGS ========= //
// ========================================== //

// Indonesian month names
const char* const monthNames[] = {
  "Januari",   // January
  "Februari",  // February
  "Maret",     // March
  "April",     // April
  "Mei",       // May
  "Juni",      // June
  "Juli",      // July
  "Agustus",   // August
  "September", // September
  "Oktober",   // October
  "November",  // November
  "Desember"   // December
};

// Indonesian day names (Sunday to Saturday)
const char* const dayWeek[] = {
  "Minggu",    // Sunday
  "Senin",     // Monday
  "Selasa",    // Tuesday
  "Rabu",      // Wednesday
  "Kamis",     // Thursday
  "Jum'at",    // Friday
  "Sabtu"      // Saturday
};

// ========================================== //
// ===== JAVANESE CALENDAR STRINGS ========== //
// ========================================== //

// Dimulai dari wage karena epoch time (1 januari 1970) adalah 'wage'
// Starts from Wage because the Unix epoch (Jan 1, 1970) was a 'Wage' day
const char* const dayPasaran[] = {
  "Wage",      // 1st Pasaran Day
  "Kliwon",    // 2nd Pasaran Day
  "Legi",      // 3rd Pasaran Day
  "Pahing",    // 4th Pasaran Day
  "Pon"        // 5th Pasaran Day
};

// ========================================== //
// ===== ISLAMIC CALENDAR STRINGS =========== //
// ========================================== //

const char* const hijriMonths[] = {
  "Muharram",       // 1st month
  "Safar",          // 2nd month
  "Rabiul Awal",    // 3rd month (Rabi' al-Awwal)
  "Rabiul Akhir",   // 4th month (Rabi' al-Thani)
  "Jumadil Awal",   // 5th month (Jumada al-Awwal)
  "Jumadil Akhir",  // 6th month (Jumada al-Thani)
  "Rajab",          // 7th month
  "Sya'ban",        // 8th month (Sha'ban)
  "Ramadhan",       // 9th month (Fasting month)
  "Syawal",         // 10th month (Shawwal)
  "Dzulqa'dah",     // 11th month (Dhu al-Qadah)
  "Dzulhijjah"      // 12th month (Dhu al-Hijjah)
};