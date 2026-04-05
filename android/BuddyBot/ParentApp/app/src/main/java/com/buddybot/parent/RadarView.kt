package com.buddybot.parent

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View

class RadarView(context: Context, attrs: AttributeSet) : View(context, attrs) {
    private val paint = Paint().apply {
        color = Color.GREEN
        style = Paint.Style.STROKE
        strokeWidth = 3f
        isAntiAlias = true
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = width / 2f
        val cy = height / 2f
        val radius = minOf(cx, cy) - 20f
        canvas.drawCircle(cx, cy, radius, paint)
        canvas.drawCircle(cx, cy, radius / 2, paint)
        canvas.drawLine(cx, cy - radius, cx, cy + radius, paint)
        canvas.drawLine(cx - radius, cy, cx + radius, cy, paint)
    }
}