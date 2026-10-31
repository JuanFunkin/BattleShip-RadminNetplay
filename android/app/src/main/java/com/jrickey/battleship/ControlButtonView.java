package com.jrickey.battleship;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

/** A round on-screen button drawn with a Canvas (colour, opacity and size come from the user's layout). */
final class ControlButtonView extends View {

    interface PressListener { void onPress(boolean down); }
    interface EditListener { void onSelect(ControlButtonView v); void onMoved(ControlButtonView v); void onDrop(ControlButtonView v); }

    final TouchLayoutConfig cfg;
    final TouchLayoutConfig.Item item;   // null for fixed buttons (menu / settings)
    final String label;
    final int color;
    final float baseDp;
    private final float density;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint sel  = new Paint(Paint.ANTI_ALIAS_FLAG);

    PressListener pressListener;
    EditListener editListener;
    boolean editMode = false;
    boolean selected = false;
    private boolean pressed = false;
    private float dragDx, dragDy;

    ControlButtonView(Context ctx, TouchLayoutConfig cfg, TouchLayoutConfig.Item item, String label, int color, float baseDp) {
        super(ctx);
        this.cfg = cfg; this.item = item; this.label = label; this.color = color; this.baseDp = baseDp;
        density = ctx.getResources().getDisplayMetrics().density;
        ring.setStyle(Paint.Style.STROKE);
        ring.setColor(Color.WHITE);
        text.setColor(Color.WHITE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        text.setShadowLayer(3f * density, 0f, 1f * density, Color.argb(160, 0, 0, 0));
        sel.setStyle(Paint.Style.STROKE);
        sel.setColor(Color.rgb(255, 214, 10));
        sel.setStrokeWidth(2.5f * density);
        sel.setPathEffect(new DashPathEffect(new float[] { 8f * density, 6f * density }, 0));
    }

    /** Diameter in pixels with the user's size settings applied. */
    int diameterPx() {
        float s = baseDp * density;
        if (item != null) s *= cfg.sizeScale * item.scale;
        return Math.round(s);
    }

    @Override
    protected void onDraw(Canvas c) {
        float w = getWidth(), h = getHeight();
        float cx = w / 2f, cy = h / 2f, r = Math.min(w, h) / 2f - 3f * density;
        float a = cfg.opacity;
        boolean hidden = item != null && !item.visible;
        if (hidden) a *= 0.35f;
        float k = pressed ? 1.25f : 1f;
        int lo = Color.argb((int) (255 * a * 0.55f), Color.red(color), Color.green(color), Color.blue(color));
        int hi = Color.argb((int) (255 * a), Math.min(255, (int) (Color.red(color) * k + 40)),
                Math.min(255, (int) (Color.green(color) * k + 40)), Math.min(255, (int) (Color.blue(color) * k + 40)));
        fill.setShader(new RadialGradient(cx, cy - r * 0.25f, r * 1.2f, hi, lo, Shader.TileMode.CLAMP));
        c.drawCircle(cx, cy, r, fill);
        ring.setStrokeWidth((pressed ? 3.5f : 2.2f) * density);
        ring.setAlpha((int) (255 * Math.min(1f, a + 0.15f)));
        c.drawCircle(cx, cy, r, ring);
        if (label != null) {
            text.setTextSize(label.length() <= 1 ? r * 0.95f : (label.length() <= 3 ? r * 0.62f : r * 0.46f));
            text.setAlpha((int) (255 * Math.min(1f, a + 0.25f)));
            c.drawText(label, cx, cy - (text.descent() + text.ascent()) / 2f, text);
        }
        if (editMode && selected) {
            c.drawCircle(cx, cy, r + 3f * density, sel);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        if (editMode && item != null) return handleEditTouch(ev);
        if (pressListener == null) return false;
        switch (ev.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (!pressed) {
                    pressed = true;
                    if (cfg.haptics) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                    pressListener.onPress(true);
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                if (pressed) {
                    pressed = false;
                    pressListener.onPress(false);
                    invalidate();
                }
                return true;
            default:
                return true;
        }
    }

    private boolean handleEditTouch(MotionEvent ev) {
        View parent = (View) getParent();
        if (parent == null) return false;
        switch (ev.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                dragDx = ev.getRawX() - (getX() + getWidth() / 2f);
                dragDy = ev.getRawY() - (getY() + getHeight() / 2f);
                // raw coords are screen-based; compensate with the parent's screen origin
                int[] loc = new int[2];
                parent.getLocationOnScreen(loc);
                dragDx -= loc[0]; dragDy -= loc[1];
                if (editListener != null) editListener.onSelect(this);
                return true;
            case MotionEvent.ACTION_MOVE: {
                int[] l2 = new int[2];
                parent.getLocationOnScreen(l2);
                float px = ev.getRawX() - l2[0] - dragDx;
                float py = ev.getRawY() - l2[1] - dragDy;
                item.x = TouchLayoutConfig.clamp(px / parent.getWidth(), 0.02f, 0.98f);
                item.y = TouchLayoutConfig.clamp(py / parent.getHeight(), 0.04f, 0.96f);
                parent.requestLayout();
                if (editListener != null) editListener.onMoved(this);
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (editListener != null) editListener.onDrop(this);
                return true;
            default:
                return true;
        }
    }
}
