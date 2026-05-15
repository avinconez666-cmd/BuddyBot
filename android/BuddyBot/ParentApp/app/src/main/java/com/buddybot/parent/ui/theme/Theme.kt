package com.buddybot.parent.ui.theme

import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import androidx.compose.material3.Typography

// ── Colour palette ────────────────────────────────────────────────────────
val BgDeep    = Color(0xFF020810)
val Surface   = Color(0xFF060F1E)
val Card      = Color(0xFF0A1628)
val Border    = Color(0xFF0D2545)
val Cyan      = Color(0xFF00D4FF)
val CyanDim   = Color(0xFF007A99)
val CyanDeep  = Color(0xFF003D4D)
val Mint      = Color(0xFF00FF9D)
val MintDeep  = Color(0xFF00663D)
val Amber     = Color(0xFFFFB800)
val AmberDeep = Color(0xFF664A00)
val Coral     = Color(0xFFFF3355)
val CoralDeep = Color(0xFF661522)
val Purple    = Color(0xFF7C3AED)
val Magenta   = Color(0xFFFF00FF)
val GrayDark  = Color(0xFF2A4060)
val GrayLight = Color(0xFF5A7A9A)
val WhiteSoft = Color(0xFFC8E0FF)

val HudTypography = Typography(
    displayLarge  = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Black,  fontSize = 28.sp),
    displayMedium = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,   fontSize = 22.sp),
    titleLarge    = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,   fontSize = 16.sp, letterSpacing = 2.sp),
    titleMedium   = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold, fontSize = 12.sp, letterSpacing = 1.5.sp),
    bodyMedium    = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Normal, fontSize = 11.sp),
    bodySmall     = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Normal, fontSize = 9.sp,  letterSpacing = 1.sp),
    labelSmall    = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Normal, fontSize = 8.sp,  letterSpacing = 2.sp),
)

private val colorScheme = darkColorScheme(
    background       = BgDeep,
    surface          = Surface,
    surfaceVariant   = Card,
    primary          = Cyan,
    secondary        = Mint,
    tertiary         = Purple,
    onBackground     = WhiteSoft,
    onSurface        = WhiteSoft,
    onPrimary        = BgDeep,
    outline          = Border,
    error            = Coral,
)

@Composable
fun BuddyBotParentTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = colorScheme,
        typography  = HudTypography,
        content     = content,
    )
}
