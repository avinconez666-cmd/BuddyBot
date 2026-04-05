package com.buddybot.kids

import android.content.Context
import android.util.AttributeSet
import android.widget.VideoView

/**
 * Custom VideoView to force full screen stretching (removes black bars)
 */
class FullScreenVideoView : VideoView {
    constructor(context: Context) : super(context)
    constructor(context: Context, attrs: AttributeSet) : super(context, attrs)
    constructor(context: Context, attrs: AttributeSet, defStyle: Int) : super(context, attrs, defStyle)

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val width = getDefaultSize(0, widthMeasureSpec)
        val height = getDefaultSize(0, heightMeasureSpec)
        setMeasuredDimension(width, height)
    }
}
