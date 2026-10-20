package org.d2r.android.port;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.PopupWindow;

import java.io.IOException;
import java.io.InputStream;

/**
 * Draw-only visual layer for the actual D2R live touch core.
 *
 * This View never receives touches and never synthesizes input: the original
 * NativeActivity AInputQueue remains the only input path. Every coordinate is
 * read from the core layout snapshot published by frontend_activity.c, so the
 * circles match the same logical layout used by the gesture core. The game
 * provides the skill/HUD glyphs; where no Android glyph path exists we draw an
 * explicit "?" placeholder rather than inventing a skill icon or cooldown.
 *
 * Socket treatment is adapted from the upstream MIT-licensed
 * DmTouchControlArt.h / dm_mobile_socket.png (commit 46cb318), translated to
 * Canvas gradients so Android does not need the iOS UIKit asset pipeline.
 */
final class D2RTouchOverlay extends View {
    private final D2RActivity activity;
    private final PopupWindow popup;
    private final View decor;
    private final float[] values = new float[96];
    private final long[] state = new long[4];
    private final int[] glyphs = new int[20];
    private final int[] atlasInfo = new int[45];
    private final float[] riftValues = new float[16];
    private final String[] riftLines = new String[4];
    private final Bitmap[] atlases = new Bitmap[TouchGlyph.ATLAS_COUNT];
    private final Bitmap[] glyphCache = new Bitmap[TouchGlyph.ATLAS_COUNT * 128];
    private final int[] glyphRect = new int[4];
    private final Rect src = new Rect();
    private final Rect dst = new Rect();
    private boolean atlasInfoLoaded;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint hudText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final View.OnLayoutChangeListener layoutListener;
    private boolean visible, destroyed;

    D2RTouchOverlay(D2RActivity activity) {
        super(activity);
        this.activity = activity;
        this.decor = activity.getWindow().getDecorView();
        setWillNotDraw(false);
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        fill.setColor(0xaa10161c);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(Math.max(1f, getResources().getDisplayMetrics().density * 1.5f));
        stroke.setColor(0xccd8c49a);
        label.setColor(0xfff1e8d8);
        label.setTextAlign(Paint.Align.CENTER);
        label.setTextSize(getResources().getDisplayMetrics().density * 11f);
        popup = new PopupWindow(this, android.view.ViewGroup.LayoutParams.MATCH_PARENT,
            android.view.ViewGroup.LayoutParams.MATCH_PARENT, false);
        popup.setTouchable(false);
        popup.setFocusable(false);
        popup.setOutsideTouchable(false);
        popup.setClippingEnabled(false);
        popup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        popup.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        layoutListener = (v, l, t, r, b, oldL, oldT, oldR, oldB) -> {
            if (visible && !destroyed && (r - l) > 0 && (b - t) > 0) {
                popup.update(0, 0, r - l, b - t);
                invalidate();
            }
        };
        decor.addOnLayoutChangeListener(layoutListener);
    }

    void setVisible(boolean value) {
        if (destroyed) return;
        if (visible == value && (!value || popup.isShowing())) return;
        visible = value;
        if (value) {
            if (!decor.isAttachedToWindow()) {
                decor.post(() -> { if (!destroyed && visible) setVisible(true); });
                return;
            }
            if (!popup.isShowing()) popup.showAtLocation(decor, Gravity.TOP | Gravity.LEFT, 0, 0);
            invalidate();
        } else {
            if (popup.isShowing()) popup.dismiss();
        }
    }

    void refresh() {
        if (visible && !destroyed) invalidate();
    }

    void destroy() {
        if (destroyed) return;
        destroyed = true;
        visible = false;
        popup.dismiss();
        decor.removeOnLayoutChangeListener(layoutListener);
    }

    private float mx(float[] v, float x) { return TouchOverlayGeometry.x(v, x); }
    private float my(float[] v, float y) { return TouchOverlayGeometry.y(v, y); }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!visible || destroyed) return;
        int ok;
        try { ok = activity.readLiveTouchOverlay(values, state); }
        catch (UnsatisfiedLinkError error) { return; }
        if (ok == 0 || !TouchOverlayGeometry.valid(values)) return;
        if (!atlasInfoLoaded && activity.readLiveTouchAtlasInfo(atlasInfo) != 0) atlasInfoLoaded = true;
        if (activity.readLiveTouchGlyphSnapshot(glyphs) == 0) {
            for (int i = 0; i < glyphs.length; i++) glyphs[i] = i % 4 == 0 ? TouchGlyph.UNKNOWN : -1;
        }
        float[] v = values;
        if (TouchOverlayGeometry.frontend(v)) return; // original front-end pages have no combat controls
        int mask = (int)v[17];
        int skillSet = (int)v[15];
        int players = (int)v[18];
        boolean navigation = TouchOverlayGeometry.navigation(v);

        // Joystick: the base follows the actual floating origin while a core joystick finger is down.
        // The vertical sign matches DmTouchOverlay: dy = -ly / 32767 * radius.
        if (v[8] > 0 && !navigation && TouchOverlayGeometry.joyActive(v)) {
            float cx = mx(v, TouchOverlayGeometry.joyBaseX(v));
            float cy = my(v, TouchOverlayGeometry.joyBaseY(v));
            float r = v[8] * v[2] / v[4];
            circle(canvas, cx, cy, r, 0x00141b22, 0x38d8c49a);
            float kx = mx(v, TouchOverlayGeometry.joyKnobX(v, state[1]));
            float ky = my(v, TouchOverlayGeometry.joyKnobY(v, state[2]));
            circle(canvas, kx, ky, r * .32f, 0x30d8c49a, 0x60f1e8d8);
        }

        // Main attack and the four skill slots are the real target centers. A bound skill uses the
        // original iOS native art mapping (dm_ta_current_set) when the local generated atlas is present;
        // empty slots draw a cross and unknown/missing assets remain an explicit "?" rather than a fake icon.
        for (int i = 0; i < 12; i++) {
            if ((mask & (1 << i)) == 0) continue;
            // As on iOS, belt and menu artwork belongs to the native HUD.
            if (i >= 6 && i <= 10) continue;
            float cx = mx(v, v[24 + i * 3]);
            float cy = my(v, v[25 + i * 3]);
            float r = v[26 + i * 3] * v[2] / v[4];
            if (r <= 0) continue;
            int glyphStatus = i <= 4 ? glyphs[i * 4] : TouchGlyph.GLYPH;
            boolean empty = i <= 4 && glyphStatus == TouchGlyph.EMPTY;
            if (i <= 5) {
                drawSocket(canvas, cx, cy, r, (state[0] & TouchGlyph.button(i)) != 0,
                           empty, i == 0);
            } else {
                int bg = (i == 10) ? 0x995a4430 : 0x77101820;
                circle(canvas, cx, cy, r, bg, i == 0 ? 0xeed8c49a : 0xaad8c49a);
            }
            if (i <= 4) {
                if (!navigation && drawSkillGlyph(canvas, i, cx, cy, r, state[0])) continue;
                text(canvas, "?", cx, cy + label.getTextSize() * .36f);
                continue;
            }
            String s;
            if (i == 5) s = "TP";
            else if (i >= 6 && i <= 9) s = String.valueOf(i - 5);
            else if (i == 10) s = "MENU";
            else s = "MAP";
            if (i >= 6 && i <= 9) {
                int count = (int)v[19 + (i - 6)];
                if (count > 0) s += ":" + count;
            }
            text(canvas, s, cx, cy + label.getTextSize() * .36f);
        }

        // Skill group toggle is the core's swap target, not a reinvented layout.
        if (!navigation && v[11] > 0) {
            float cx = mx(v, v[9]), cy = my(v, v[10]), r = v[11] * v[2] / v[4];
            circle(canvas, cx, cy, r, 0x88101820, 0xccd8c49a);
            text(canvas, skillSet == 0 ? "I" : "II", cx, cy + label.getTextSize() * .36f);
        }

        // Difficulty target: host-reported player count; the eight choices are the core's pick cells.
        if (!navigation && v[14] > 0) {
            float cx = mx(v, v[12]), cy = my(v, v[13]), r = v[14] * v[2] / v[4];
            text(canvas, players > 0 ? "P" + players : "P?", cx, cy + label.getTextSize() * .36f);
            if ((int)v[16] != 0) {
                for (int i = 0; i < 8; i++) {
                    float px = mx(v, v[60 + i * 3]), py = my(v, v[61 + i * 3]);
                    float pr = v[62 + i * 3] * v[2] / v[4];
                    if (pr <= 0) continue;
                    circle(canvas, px, py, pr, players == i + 1 ? 0x99725436 : 0x88101820, 0xccd8c49a);
                    text(canvas, String.valueOf(i + 1), px, py + label.getTextSize() * .36f);
                }
            }
        }
        if (TouchRift.visible((int)v[23], (int)v[89],
                activity.readLiveTouchRiftSnapshot(riftValues, riftLines))) {
            drawRift(canvas, v, riftValues, riftLines);
        }
    }

    private void drawRift(Canvas canvas, float[] v, float[] r, String[] lines) {
        float u = v[3] / 614f;
        if (!(u > 0f)) return;
        float x = v[0] + 28f * u, y = v[1] + 150f * u, w = 250f * u, h = 78f * u;
        if (!(w > 0f && h > 0f)) return;
        int gold = 0xffc7b377, red = 0xffc83228, white = 0xffd7d2c8, grey = 0xff969187;
        boolean guardian = (int)r[5] != 0;
        boolean done = (int)r[4] != 0;
        int secs = (int)r[3]; if (secs < 0) secs = 0;
        float progress = done ? 1f : Math.max(0f, Math.min(1f, r[2]));

        fill.setShader(null);
        fill.setColor(0xaa000000);
        canvas.drawRect(x, y, x + w, y + h, fill);
        fill.setColor(0xe678643c);
        canvas.drawRect(x, y, x + w, y + Math.max(1f, u), fill);
        canvas.drawRect(x, y + h - Math.max(1f, u), x + w, y + h, fill);
        fill.setColor(0xff967d4b);
        canvas.drawRect(x, y, x + 2f * u, y + h, fill);

        hudText.setTypeface(Typeface.DEFAULT_BOLD);
        hudText.setTextAlign(Paint.Align.LEFT);
        hudText.setColor(guardian ? red : gold);
        hudText.setTextSize(17f * u);
        canvas.drawText(lines[0] != null ? lines[0] : "", x + 10f * u, y + 22f * u, hudText);
        hudText.setColor(white);
        hudText.setTextSize(14f * u);
        canvas.drawText(lines[1] != null ? lines[1] : "", x + 48f * u, y + 25f * u, hudText);
        hudText.setTextAlign(Paint.Align.RIGHT);
        hudText.setTextSize(17f * u);
        hudText.setColor(secs <= 60 && !done ? red : gold);
        String timer = lines[2] != null ? lines[2] : "";
        canvas.drawText(timer, x + w - 10f * u, y + 22f * u, hudText);
        hudText.setTextAlign(Paint.Align.LEFT);
        hudText.setTextSize(12f * u);
        hudText.setColor(grey);
        canvas.drawText(lines[3] != null ? lines[3] : "", x + 10f * u, y + 47f * u, hudText);

        float bw = Math.max(1f, u);
        RectF track = new RectF(x + 11f * u, y + 55f * u, x + w - 11f * u, y + 65f * u);
        fill.setShader(null);
        fill.setColor(0xff19140e);
        canvas.drawRect(track, fill);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(bw);
        stroke.setColor(0xff5f4e30);
        canvas.drawRect(track, stroke);
        float innerW = Math.max(0f, track.width() - 2f * bw);
        if (innerW > 0f) {
            float fw = innerW * progress;
            if (fw > 0f) {
                fill.setShader(new LinearGradient(track.left, 0f, track.right, 0f,
                    guardian ? new int[]{0xffbe3228, 0xff6e140f}
                             : new int[]{0xffcda050, 0xff8c6428},
                    null, Shader.TileMode.CLAMP));
                canvas.drawRect(track.left + bw, track.top + bw,
                                track.left + bw + fw, track.bottom - bw, fill);
                fill.setShader(null);
            }
        }
        stroke.setStrokeWidth(Math.max(1f, u));
        stroke.setColor(0x6e000000);
        for (int i = 0; i < 9; i++) {
            float tx = track.left + bw + innerW * (i + 1) / 10f;
            canvas.drawLine(tx, track.top + bw, tx, track.bottom - bw, stroke);
        }
        for (int i = 0; i < 2; i++) {
            float cx = i == 0 ? track.left - 4f * u : track.right + 4f * u;
            float cy = (track.top + track.bottom) * .5f, rad = 4f * u;
            Path pin = new Path();
            pin.moveTo(cx, cy - rad); pin.lineTo(cx + rad, cy); pin.lineTo(cx, cy + rad);
            pin.lineTo(cx - rad, cy); pin.close();
            fill.setShader(null);
            fill.setColor(0xffa08750);
            canvas.drawPath(pin, fill);
            stroke.setStrokeWidth(Math.max(1f, u * .8f));
            stroke.setColor(0xff3c301e);
            canvas.drawPath(pin, stroke);
        }
    }
    private void drawSocket(Canvas canvas, float x, float y, float r,
                            boolean held, boolean empty, boolean primary) {
        // Upstream socket: narrow iron rim over a charcoal well, with only a
        // silver held edge. Procedural gradients keep this draw-only overlay
        // independent of the iOS PNG/UIKit pipeline.
        float side = r * 2f;
        RectF rim = new RectF(x - r + side * .018f, y - r + side * .018f,
                              x + r - side * .018f, y + r - side * .018f);
        RectF face = new RectF(x - r + side * .055f, y - r + side * .055f,
                               x + r - side * .055f, y + r - side * .055f);
        fill.setShader(new LinearGradient(x, rim.top, x, rim.bottom,
            new int[]{TouchSocketColor.argb(.26f, .28f, .28f, empty ? .22f : .9f),
                      TouchSocketColor.argb(.055f, .065f, .07f, empty ? .22f : .95f),
                      TouchSocketColor.argb(.16f, .11f, .10f, empty ? .16f : .7f)},
            new float[]{0f, .5f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawOval(rim, fill);
        fill.setShader(null);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(side * .025f);
        stroke.setColor(TouchSocketColor.argb(.008f, .012f, .015f, empty ? .38f : .9f));
        canvas.drawOval(rim, stroke);
        fill.setShader(new LinearGradient(x, face.top, x, face.bottom,
            new int[]{TouchSocketColor.argb(.021f, .021f, .021f, empty ? .08f : .94f),
                      TouchSocketColor.argb(.050f, .050f, .050f, empty ? .16f : .94f)},
            null, Shader.TileMode.CLAMP));
        canvas.drawOval(face, fill);
        fill.setShader(null);
        stroke.setStrokeWidth(TouchSocketColor.faceWidth(side, held, empty));
        stroke.setColor(TouchSocketColor.faceColor(held, empty));
        canvas.drawOval(face, stroke);
        if (primary && !empty) {
            stroke.setStrokeWidth(side * .012f);
            stroke.setColor(TouchSocketColor.argb(.31f, .23f, .22f, .52f));
            canvas.drawOval(new RectF(x - r + side * .035f, y - r + side * .035f,
                                      x + r - side * .035f, y + r - side * .035f), stroke);
        }
    }

    private void drawPlus(Canvas canvas, float x, float y, float r, boolean held) {
        stroke.setColor(TouchSocketColor.emptyCrossColor(held));
        stroke.setStrokeWidth(r * .036f);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        float a = r * .11f;
        canvas.drawLine(x - a, y, x + a, y, stroke);
        canvas.drawLine(x, y - a, x, y + a, stroke);
        stroke.setStrokeCap(Paint.Cap.BUTT); // CoreGraphics save/restore: do not leak ROUND
    }

    private Bitmap loadAtlas(int atlas) {
        if (atlas < 0 || atlas >= TouchGlyph.ATLAS_COUNT) return null;
        Bitmap cached = atlases[atlas];
        if (cached != null) return cached;
        String name = TouchGlyph.assetName(atlas);
        if (name == null || !atlasInfoLoaded) return null;
        InputStream in = null;
        try {
            in = activity.getAssets().open(name);
            cached = BitmapFactory.decodeStream(in);
            atlases[atlas] = cached;
        } catch (IOException | RuntimeException error) {
            cached = null;
        } finally {
            if (in != null) try { in.close(); } catch (IOException ignored) { }
        }
        return cached;
    }

    private Bitmap glyphBitmap(int atlas, int frame) {
        int key = atlas * 128 + frame;
        if (key < 0 || key >= glyphCache.length) return null;
        Bitmap cached = glyphCache[key];
        if (cached != null) return cached;
        Bitmap source = loadAtlas(atlas);
        if (source == null || frame < 0 || frame >= 128) return null;
        if (!TouchGlyph.frameRect(atlasInfo, atlas, frame, glyphRect)) return null;
        try {
            cached = Bitmap.createBitmap(source, glyphRect[0], glyphRect[1],
                                         glyphRect[2], glyphRect[3]);
            glyphCache[key] = cached;
        } catch (IllegalArgumentException | OutOfMemoryError error) {
            cached = null;
        }
        return cached;
    }

    private boolean drawSkillGlyph(Canvas canvas, int target, float cx, float cy,
                                   float r, long buttons) {
        int base = target * 4;
        if (base < 0 || base + 3 >= glyphs.length) return false;
        int status = glyphs[base];
        if (status == TouchGlyph.EMPTY) {
            drawPlus(canvas, cx, cy, r, (buttons & TouchGlyph.button(target)) != 0);
            return true;
        }
        if (status != TouchGlyph.GLYPH) return false;
        int atlas = glyphs[base + 1];
        int frame = (buttons & TouchGlyph.button(target)) != 0 ? glyphs[base + 3] : glyphs[base + 2];
        if (!TouchGlyph.validAtlasInfo(atlasInfo) ||
            !TouchGlyph.frameRect(atlasInfo, atlas, frame, glyphRect)) return false;
        Bitmap glyph = glyphBitmap(atlas, frame);
        if (glyph == null) return false;
        // glyphBitmap already cropped the atlas; its coordinates start at zero.
        src.set(0, 0, glyph.getWidth(), glyph.getHeight());
        float size = r * (target == 0 ? 1.52f : 1.36f);
        dst.set(Math.round(cx - size * .5f), Math.round(cy - size * .5f),
                Math.round(cx + size * .5f), Math.round(cy + size * .5f));
        Path clip = new Path();
        clip.addCircle(cx, cy, r * .86f, Path.Direction.CW);
        int save = canvas.save();
        canvas.clipPath(clip);
        canvas.drawBitmap(glyph, src, dst, null);
        canvas.restoreToCount(save);
        return true;
    }

    private void circle(Canvas canvas, float x, float y, float r, int bg, int line) {
        fill.setColor(bg);
        stroke.setColor(line);
        canvas.drawCircle(x, y, Math.max(1f, r), fill);
        canvas.drawCircle(x, y, Math.max(1f, r), stroke);
    }

    private void text(Canvas canvas, String s, float x, float y) {
        canvas.drawText(s, x, y, label);
    }
}
