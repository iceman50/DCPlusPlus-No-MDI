// Copyright (C) 2026 iceman50
// Original DC++ interface artwork. Coordinates use a 24-unit optical grid.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

public static partial class IconPackRenderer {
    public static readonly int[] Sizes = { 16, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256 };

    private sealed class Palette {
        public Color Ink, Blue, Green, Red, Gold, Violet, Muted, Background;
        public int Fill;
        public bool Solid, Neon;
        public Palette(string theme) {
            switch(theme) {
                case "Neon-Circuit":
                    Ink = Hex("c5f6ff"); Blue = Hex("26cfff"); Green = Hex("86f45b");
                    Red = Hex("ff5b96"); Gold = Hex("ffbd62"); Violet = Hex("bd7bff");
                    Muted = Hex("7787a1"); Background = Hex("101522"); Fill = 32; Neon = true; break;
                case "Dark":
                    Ink = Hex("dce6f3"); Blue = Hex("79b8ff"); Green = Hex("78d6ab");
                    Red = Hex("ff8b96"); Gold = Hex("edc27e"); Violet = Hex("b6a2ef");
                    Muted = Hex("9aa9ba"); Background = Hex("20252d"); Fill = 45; break;
                case "Aurora":
                    Ink = Hex("e1f7f5"); Blue = Hex("62d9df"); Green = Hex("88e6b5");
                    Red = Hex("ff94b2"); Gold = Hex("f3d38c"); Violet = Hex("c0a4ff");
                    Muted = Hex("99adc2"); Background = Hex("171d2c"); Fill = 100; break;
                case "Copper":
                    Ink = Hex("f5e6d7"); Blue = Hex("dfac86"); Green = Hex("b3cea2");
                    Red = Hex("f29887"); Gold = Hex("ebc786"); Violet = Hex("c8aec4");
                    Muted = Hex("b7aaa3"); Background = Hex("292422"); Fill = 255; Solid = true; break;
                case "Paper":
                    Ink = Hex("344255"); Blue = Hex("2866a8"); Green = Hex("20764f");
                    Red = Hex("b83d4f"); Gold = Hex("93621a"); Violet = Hex("7755a3");
                    Muted = Hex("687586"); Background = Hex("f5f7fa"); Fill = 24; break;
                default: throw new ArgumentException("Unknown icon palette: " + theme);
            }
        }
        public Color Surface(Color color) {
            return Solid ? Color.FromArgb((color.R + Background.R * 3) / 4,
                (color.G + Background.G * 3) / 4, (color.B + Background.B * 3) / 4) : Color.FromArgb(Fill, color);
        }
    }

    private static Color Hex(string value) { return ColorTranslator.FromHtml("#" + value); }

    private sealed class Canvas : IDisposable {
        private readonly Bitmap bitmap;
        private readonly Graphics g;
        private readonly float unit;
        private readonly int size;
        private bool drawingBadge;
        public readonly Palette P;
        public float Stroke { get; private set; }
        public bool Small { get { return size <= 22; } }
        public Canvas(int size, Palette palette) {
            this.size = size; P = palette;
            int scale = size <= 48 ? 4 : 2;
            bitmap = new Bitmap(size * scale, size * scale, PixelFormat.Format32bppArgb);
            g = Graphics.FromImage(bitmap);
            g.Clear(Color.Transparent);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.CompositingQuality = CompositingQuality.HighQuality;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            unit = size * scale / 24f;
            // A 16 px icon needs 1.25 px strokes; larger icons keep a lighter relative weight.
            Stroke = size <= 16 ? 1.875f : size <= 22 ? 1.8f : 1.65f;
        }
        private PointF Pt(float x, float y) { return new PointF(x * unit, y * unit); }
        private Pen Pen(Color color, float width) {
            return new Pen(color, width * unit) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round };
        }
        private void StrokePath(Color color, Action<Pen> draw) {
            using(Pen pen = Pen(color, Stroke)) draw(pen);
            if(P.Neon && size >= 24 && !drawingBadge && color != P.Muted && color != P.Background) {
                Color hot = Color.FromArgb(200, (color.R + 255) / 2, (color.G + 255) / 2, (color.B + 255) / 2);
                using(Pen pen = Pen(hot, Stroke * .34f)) draw(pen);
            }
        }
        public void Line(Color color, params float[] points) {
            PointF[] p = new PointF[points.Length / 2];
            for(int i = 0; i < p.Length; i++) p[i] = Pt(points[i * 2], points[i * 2 + 1]);
            StrokePath(color, pen => g.DrawLines(pen, p));
        }
        public void Curve(Color color, params float[] p) {
            StrokePath(color, pen => g.DrawBezier(pen, Pt(p[0],p[1]), Pt(p[2],p[3]), Pt(p[4],p[5]), Pt(p[6],p[7])));
        }
        public void Arc(Color color, float x, float y, float w, float h, float start, float sweep) {
            StrokePath(color, pen => g.DrawArc(pen, x*unit,y*unit,w*unit,h*unit,start,sweep));
        }
        public void RingArrow(Color color, float radius, float start, float sweep) {
            // One filled silhouette: a constant-width ring flows into the base
            // of a triangular head. No overlapping round caps or doubled strokes.
            float outer = radius + Stroke / 2, inner = radius - Stroke / 2;
            double angle = (start + sweep) * Math.PI / 180;
            float nx = (float)Math.Cos(angle), ny = (float)Math.Sin(angle);
            float x = 12 + radius * nx, y = 12 + radius * ny;
            float headLength = 3.4f, halfWidth = 2.5f;
            using(GraphicsPath path = new GraphicsPath()) {
                path.AddArc((12-outer)*unit,(12-outer)*unit,outer*2*unit,outer*2*unit,start,sweep);
                path.AddLine(Pt(12+outer*nx,12+outer*ny),Pt(x+halfWidth*nx,y+halfWidth*ny));
                path.AddLine(Pt(x+halfWidth*nx,y+halfWidth*ny),Pt(x-headLength*ny,y+headLength*nx));
                path.AddLine(Pt(x-headLength*ny,y+headLength*nx),Pt(x-halfWidth*nx,y-halfWidth*ny));
                path.AddLine(Pt(x-halfWidth*nx,y-halfWidth*ny),Pt(12+inner*nx,12+inner*ny));
                path.AddArc((12-inner)*unit,(12-inner)*unit,inner*2*unit,inner*2*unit,start+sweep,-sweep);
                path.CloseFigure();
                using(SolidBrush brush = new SolidBrush(color)) g.FillPath(brush,path);
            }
        }
        private void Shape(GraphicsPath path, Color color, bool fill, bool solid = false) {
            if(fill) using(SolidBrush brush = new SolidBrush(solid ? color : P.Surface(color))) g.FillPath(brush, path);
            StrokePath(color, pen => g.DrawPath(pen, path));
        }
        public void Polygon(Color color, bool fill, params float[] points) {
            PointF[] p = new PointF[points.Length / 2];
            for(int i = 0; i < p.Length; i++) p[i] = Pt(points[i * 2], points[i * 2 + 1]);
            using(GraphicsPath path = new GraphicsPath()) { path.AddPolygon(p); Shape(path, color, fill); }
        }
        public void Circle(Color color, float x, float y, float r, bool fill) {
            using(GraphicsPath path = new GraphicsPath()) {
                path.AddEllipse((x-r)*unit,(y-r)*unit,2*r*unit,2*r*unit); Shape(path,color,fill);
            }
        }
        public void Dot(Color color, float x, float y, float r) {
            using(SolidBrush brush = new SolidBrush(color)) g.FillEllipse(brush,(x-r)*unit,(y-r)*unit,2*r*unit,2*r*unit);
        }
        public void Box(Color color, float x, float y, float w, float h, float r, bool fill, bool solid = false) {
            using(GraphicsPath path = new GraphicsPath()) {
                float d=r*2*unit, left=x*unit, top=y*unit, width=w*unit, height=h*unit;
                path.AddArc(left,top,d,d,180,90); path.AddArc(left+width-d,top,d,d,270,90);
                path.AddArc(left+width-d,top+height-d,d,d,0,90); path.AddArc(left,top+height-d,d,d,90,90);
                path.CloseFigure(); Shape(path,color,fill,solid);
            }
        }
        public void CornerBadge(string symbol, Color color, float x, float y) {
            GraphicsState saved = g.Save();
            try {
                g.TranslateTransform((x-18)*unit,(y-18)*unit);
                Badge(symbol,color);
            } finally { g.Restore(saved); }
        }
        public void BotOverlay() {
            drawingBadge = true;
            float normalStroke = Stroke;
            Stroke = 1.1f;
            Line(P.Violet,6.5f,2.5f,6.5f,4);
            Box(P.Violet,2.5f,4,8,6,1.2f,true,true);
            Dot(P.Background,4.6f,6.8f,.85f);
            Dot(P.Background,8.4f,6.8f,.85f);
            Stroke = normalStroke;
            drawingBadge = false;
        }
        public void Badge(string symbol, Color color) {
            drawingBadge = true;
            // Cut away the underlying glyph. The transparent moat works on any app surface.
            g.CompositingMode = CompositingMode.SourceCopy;
            Dot(Color.Transparent,18,18,5.0f);
            g.CompositingMode = CompositingMode.SourceOver;
            Dot(color,18,18,3.8f);
            Color ink = P.Background;
            float normalStroke = Stroke;
            Stroke = Small ? 1.25f : 1.35f;
            switch(symbol) {
                case "check": Line(ink,16.1f,18,17.4f,19.3f,20,16.8f); break;
                case "minus": Line(ink,16.2f,18,19.8f,18); break;
                case "cross": Line(ink,16.6f,16.6f,19.4f,19.4f); Line(ink,19.4f,16.6f,16.6f,19.4f); break;
                case "down": Line(ink,18,15.9f,18,20); Line(ink,16.3f,18.3f,18,20,19.7f,18.3f); break;
                case "up": Line(ink,18,20,18,15.9f); Line(ink,16.3f,17.6f,18,15.9f,19.7f,17.6f); break;
                case "clock": Line(ink,18,16,18,18,19.6f,18.8f); break;
                case "pause": Line(ink,16.8f,16.4f,16.8f,19.6f); Line(ink,19.2f,16.4f,19.2f,19.6f); break;
                case "plus": Line(ink,16.1f,18,19.9f,18); Line(ink,18,16.1f,18,19.9f); break;
                case "star": Star(this,ink,18,18,2.3f,false); break;
                case "search": Circle(ink,17.4f,17.4f,1.55f,false); Line(ink,18.7f,18.7f,20,20); break;
                case "user": Dot(ink,18,16.8f,1.0f); Arc(ink,16.2f,18,3.6f,3,180,180); break;
                case "lock": Box(ink,16.2f,17.6f,3.6f,2.5f,.4f,false); Arc(ink,16.9f,15.5f,2.2f,3.5f,180,180); break;
                case "dot": break;
                default: throw new ArgumentException("Unknown badge " + symbol);
            }
            Stroke = normalStroke;
            drawingBadge = false;
        }
        public Bitmap Finish() {
            Bitmap output = new Bitmap(size,size,PixelFormat.Format32bppArgb);
            using(Graphics target = Graphics.FromImage(output)) {
                target.CompositingMode = CompositingMode.SourceCopy;
                target.CompositingQuality = CompositingQuality.HighQuality;
                target.InterpolationMode = InterpolationMode.HighQualityBicubic;
                target.PixelOffsetMode = PixelOffsetMode.HighQuality;
                target.DrawImage(bitmap,new Rectangle(0,0,size,size),0,0,bitmap.Width,bitmap.Height,GraphicsUnit.Pixel);
            }
            if(P.Neon && size >= 24) {
                Bitmap illuminated = NeonHalo(output);
                output.Dispose();
                return illuminated;
            }
            return output;
        }
        public void Dispose() { g.Dispose(); bitmap.Dispose(); }
    }

    public static Bitmap Render(string fileName, int size, string theme) {
        using(Canvas c = new Canvas(size,new Palette(theme))) {
            string name = Path.GetFileNameWithoutExtension(fileName).ToLowerInvariant();
            if(!c.P.Neon || !DrawNeon(c,name)) Draw(c,name);
            return c.Finish();
        }
    }

    private static void Star(Canvas c, Color color, float x, float y, float radius, bool fill) {
        float[] points = new float[20];
        for(int i=0;i<10;i++) {
            double a = Math.PI*i/5-Math.PI/2; float r = i%2==0 ? radius : radius*.46f;
            points[i*2]=x+(float)Math.Cos(a)*r; points[i*2+1]=y+(float)Math.Sin(a)*r;
        }
        c.Polygon(color,fill,points);
    }
    private static void Check(Canvas c, Color color) { c.Line(color,7,12,10.5f,15.5f,17,8.8f); }
    private static void Arrow(Canvas c, Color color, bool up, bool tray) {
        float tip=up ? 4 : 16, tail=up ? 16 : 4, wing=up ? 9 : 11;
        c.Line(color,12,tail,12,tip); c.Line(color,7,wing,12,tip,17,wing);
        if(tray) c.Line(c.P.Muted,4,16,4,20,20,20,20,16);
    }
    private static void Person(Canvas c, Color color, string badge, Color badgeColor) {
        c.Circle(color,11,7,3.3f,true);
        c.Curve(color,4,20,4,11,18,11,18,20); c.Line(color,4,20,18,20);
        if(badge!=null) c.Badge(badge,badgeColor);
    }
    private static void People(Canvas c, bool grouped) {
        c.Circle(c.P.Muted,16.5f,7,2.7f,true); c.Arc(c.P.Muted,12,12,9,13,180,180);
        c.Circle(c.P.Blue,8.5f,9,3,true); c.Arc(c.P.Blue,2.5f,14,12,12,180,180); c.Line(c.P.Blue,2.5f,20,14.5f,20);
        if(grouped) c.Badge("plus",c.P.Violet);
    }
    private static void Folder(Canvas c, string badge) {
        c.Polygon(c.P.Gold,true,3,6,9,6,11,8,21,8,21,19,3,19);
        c.Line(c.P.Gold,3,10,21,10);
        if(badge!=null) c.Badge(badge,badge=="star" ? c.P.Gold : c.P.Blue);
    }
    private static void Document(Canvas c, string badge, bool lines) {
        c.Polygon(c.P.Blue,true,5,3,14,3,19,8,19,21,5,21);
        c.Line(c.P.Blue,14,3,14,8,19,8);
        if(lines) { c.Line(c.P.Ink,8,12,15,12); c.Line(c.P.Muted,8,16,13,16); }
        if(badge!=null) c.Badge(badge,badge=="cross" ? c.P.Red : badge=="clock" ? c.P.Gold : c.P.Blue);
    }
    private static void List(Canvas c, string badge) {
        c.Box(c.P.Blue,4,3,16,18,2,true);
        c.Dot(c.P.Ink,7.5f,7.5f,.7f); c.Line(c.P.Ink,11,7.5f,16,7.5f);
        c.Dot(c.P.Ink,7.5f,12,.7f); c.Line(c.P.Ink,11,12,16,12);
        if(!c.Small) { c.Dot(c.P.Muted,7.5f,16.5f,.7f); c.Line(c.P.Muted,11,16.5f,14,16.5f); }
        if(badge!=null) c.Badge(badge,badge=="cross" ? c.P.Red : c.P.Green);
    }
    private static void Hub(Canvas c, bool online) {
        Color color=online ? c.P.Blue : c.P.Muted;
        c.Box(color,5,3,14,6,1.6f,true); c.Dot(online ? c.P.Green : c.P.Muted,15.5f,6,.8f);
        c.Line(color,12,9,12,14); c.Line(color,5,17,5,14,19,14,19,17);
        c.Box(color,3,17,4,4,.8f,true); c.Box(color,10,17,4,4,.8f,true); c.Box(color,17,17,4,4,.8f,true);
        if(!online) c.Badge("minus",c.P.Muted);
    }
    private static void Clock(Canvas c) {
        c.Circle(c.P.Blue,12,12,8.5f,true);
        c.Line(c.P.Ink,12,7,12,12,15.5f,14);
    }
    private static void RecentWindows(Canvas c) {
        c.Box(c.P.Blue,3,4,17,15,1.5f,true);
        c.Line(c.P.Blue,3,8.5f,20,8.5f);
        c.Badge("clock",c.P.Gold);
    }
    private static void Search(Canvas c) { c.Circle(c.P.Blue,10,10,6.4f,true); c.Line(c.P.Ink,15,15,21,21); }
    private static void Reconnect(Canvas c) {
        c.RingArrow(c.P.Blue,7,200,125);
        c.RingArrow(c.P.Green,7,20,125);
    }
    private static void RefreshFileList(Canvas c) {
        c.RingArrow(c.P.Blue,7.5f,25,290);
    }
    private static void Bubble(Canvas c, bool lines) {
        c.Polygon(c.P.Blue,true,3,4,21,4,21,17,10,17,5,21,5,17,3,17);
        if(lines) { c.Line(c.P.Ink,7,8.5f,17,8.5f); c.Line(c.P.Ink,7,12.5f,13,12.5f); }
        else { c.Dot(c.P.Ink,8,10.5f,.8f); c.Dot(c.P.Ink,12,10.5f,.8f); c.Dot(c.P.Ink,16,10.5f,.8f); }
    }
    private static void Gauge(Canvas c, int direction) {
        c.Arc(c.P.Blue,3,4,18,18,150,240); c.Line(c.P.Muted,5,19,19,19);
        if(direction==0) { c.Line(c.P.Ink,12,14,16,9); c.Dot(c.P.Blue,12,14,1.3f); }
        else { float tip=direction>0 ? 9 : 16, tail=direction>0 ? 16 : 9, wing=direction>0 ? 12 : 13;
            c.Line(c.P.Ink,12,tail,12,tip); c.Line(c.P.Ink,9.5f,wing,12,tip,14.5f,wing); }
    }
    private static void Question(Canvas c, bool pointer) {
        c.Circle(c.P.Blue,12,12,8.5f,true);
        c.Curve(c.P.Ink,9,9,9,5.5f,16,5.5f,15,10);
        c.Curve(c.P.Ink,15,10,15,12,12,11.5f,12,14);
        c.Dot(c.P.Ink,12,17,.85f);
        if(pointer) c.Badge("plus",c.P.Violet);
    }
    private static void Gear(Canvas c) {
        float[] points=new float[64];
        for(int i=0;i<32;i++) { double a=Math.PI*2*i/32-Math.PI/32;
            float r=(i%4==0 || i%4==3) ? 7.2f : 9.4f;
            points[i*2]=12+(float)Math.Cos(a)*r; points[i*2+1]=12+(float)Math.Sin(a)*r; }
        c.Polygon(c.P.Violet,true,points); c.Circle(c.P.Ink,12,12,3.1f,false);
    }
    private static void Shield(Canvas c) {
        c.Polygon(c.P.Green,true,12,3,20,6,19,14,16,18,12,21,8,18,5,14,4,6); Check(c,c.P.Ink);
    }
    private static void Sound(Canvas c, bool notification) {
        c.Polygon(c.P.Blue,true,3,9,7,9,12,5,12,19,7,15,3,15);
        c.Arc(c.P.Ink,11,8,7,8,-60,120);
        if(!c.Small) c.Arc(c.P.Blue,10,4,11,16,-55,110);
        if(notification) c.Badge("dot",c.P.Gold);
    }
    private static void Draw(Canvas c, string name) {
        Palette p=c.P;
        switch(name) {
            case "publichubs":
                c.Circle(p.Blue,12,12,9,true); c.Arc(p.Blue,8,3,8,18,0,360); c.Line(p.Ink,3,12,21,12);
                if(!c.Small) { c.Arc(p.Blue,4,5,16,6,0,180); c.Arc(p.Blue,4,13,16,6,180,180); } break;
            case "search": Search(c); break;
            case "favoritehubs": Star(c,p.Gold,12,12,9,true); break;
            case "directory": Folder(c,null); break;
            case "favoritedirs": Folder(c,"star"); break;
            case "opendldir": Folder(c,"down"); break;
            case "hubon": Hub(c,true); break;
            case "huboff": Hub(c,false); break;
            case "file": Document(c,null,false); break;
            case "adlsearch": Document(c,"search",false); break;
            case "changelog": Document(c,"clock",true); break;
            case "openfilelist": Document(c,"up",true); break;
            case "openownfilelist": Document(c,"user",true); break;
            case "logs": Document(c,null,true); break;
            case "notepad":
                c.Box(p.Blue,4,4,15,17,1.5f,true); c.Line(p.Ink,8,3,8,6); c.Line(p.Ink,15,3,15,6);
                c.Line(p.Ink,8,10,15,10); c.Line(p.Muted,8,14,13,14); c.Line(p.Gold,15,20,21,12); break;
            case "queue": List(c,"down"); break;
            case "removequeue": List(c,"cross"); break;
            case "download": Arrow(c,p.Blue,false,true); break;
            case "upload": Arrow(c,p.Violet,true,true); break;
            case "finisheddl": Arrow(c,p.Blue,false,true); c.Badge("check",p.Green); break;
            case "finishedul": Arrow(c,p.Violet,true,true); c.Badge("check",p.Green); break;
            case "users": People(c,false); break;
            case "groupedbyusers": People(c,true); break;
            case "groupedbyfiles":
                c.Line(p.Muted,3,7,3,21,16,21); c.Box(p.Blue,7,3,14,14,1.5f,true);
                c.Line(p.Ink,10,7,17,7); c.Line(p.Ink,10,11,15,11); break;
            case "user": Person(c,p.Blue,null,p.Blue); break;
            case "useron": Person(c,p.Blue,"dot",p.Green); break;
            case "useroff": Person(c,p.Muted,"minus",p.Muted); break;
            case "useraway": Person(c,p.Blue,"clock",p.Gold); break;
            case "usernocon": c.CornerBadge("cross",p.Red,6,18); break;
            case "usernoslot": c.CornerBadge("pause",p.Gold,18,18); break;
            case "userop": c.CornerBadge("star",p.Gold,18,6); break;
            case "userreg": c.CornerBadge("check",p.Green,18,6); break;
            case "favoriteuseron": Person(c,p.Gold,"check",p.Green); break;
            case "favoriteuseroff": Person(c,p.Gold,"minus",p.Muted); break;
            case "userbot": c.BotOverlay(); break;
            case "netstats":
                c.Line(p.Muted,3,4,3,21,21,21); c.Line(p.Green,6,15,10,10,14,13,20,5);
                if(!c.Small) { c.Line(p.Blue,7,20,7,18); c.Line(p.Blue,12,20,12,16); c.Line(p.Blue,17,20,17,13); } break;
            case "magnet":
                c.Line(p.Blue,5,4,5,13); c.Arc(p.Blue,5,6,14,14,0,180); c.Line(p.Blue,19,13,19,4);
                c.Line(p.Ink,9,4,9,13); c.Arc(p.Ink,9,10,6,6,0,180); c.Line(p.Ink,15,13,15,4);
                c.Line(p.Red,5,4,9,4); c.Line(p.Blue,15,4,19,4); break;
            case "exit":
                c.Line(p.Muted,11,3,4,3,4,21,11,21); c.Line(p.Red,10,12,21,12); c.Line(p.Red,17,8,21,12,17,16); break;
            case "chat": Bubble(c,true); break;
            case "balloon": Bubble(c,false); break;
            case "help": Question(c,false); break;
            case "whatsthis": Question(c,true); break;
            case "reconnect": Reconnect(c); break;
            case "refresh": RefreshFileList(c); break;
            case "settings": Gear(c); break;
            case "traypm":
                c.Box(p.Blue,3,5,18,14,2,true); c.Line(p.Ink,4,7,12,13,20,7); c.Badge("dot",p.Green); break;
            case "trusted": Shield(c); break;
            case "secure":
                c.Arc(p.Green,7,3,10,13,180,180); c.Box(p.Green,4,10,16,11,2,true);
                c.Dot(p.Ink,12,14.5f,1.2f); c.Line(p.Ink,12,15,12,17.5f); break;
            case "recents": RecentWindows(c); break;
            case "clock": Clock(c); break;
            case "donate":
                c.Curve(p.Red,12,21,9,18,3,14,3,8); c.Curve(p.Red,3,8,3,2,10,2,12,7);
                c.Curve(p.Red,12,7,14,2,21,2,21,8); c.Curve(p.Red,21,8,21,14,15,18,12,21); break;
            case "getstarted":
                c.Polygon(p.Blue,true,6,12,10,5,20,3,18,13,12,17); c.Circle(p.Ink,15,8,1.8f,false);
                c.Line(p.Gold,7,17,4,20); c.Line(p.Gold,10,19,8,21); break;
            case "indexing":
                c.Box(p.Blue,4,6,16,14,3,true); c.Arc(p.Blue,4,3,16,6,0,360);
                c.Arc(p.Blue,4,9,16,5,0,180); c.Badge("search",p.Violet); break;
            case "links":
                c.Arc(p.Blue,3,8,12,8,80,280); c.Arc(p.Blue,9,8,12,8,260,280); c.Line(p.Ink,9,12,15,12); break;
            case "slots": case "slotsfull":
                c.Box(p.Muted,6,3,12,18,3,true); c.Dot(name=="slotsfull" ? p.Red : p.Muted,12,7,1.7f);
                c.Dot(p.Gold,12,12,1.7f); c.Dot(name=="slotsfull" ? p.Muted : p.Green,12,17,1.7f);
                if(name=="slotsfull") c.Badge("minus",p.Red); break;
            case "ok": case "ballgreen":
                c.Circle(p.Green,12,12,8.5f,true); Check(c,p.Green); break;
            case "cancel": case "ballred":
                c.Circle(p.Red,12,12,8.5f,true); c.Line(p.Red,8.5f,8.5f,15.5f,15.5f); c.Line(p.Red,15.5f,8.5f,8.5f,15.5f); break;
            case "increment": case "decrement":
                c.Circle(name=="increment" ? p.Green : p.Red,12,12,8.5f,true);
                c.Line(name=="increment" ? p.Green : p.Red,7.5f,12,16.5f,12);
                if(name=="increment") c.Line(p.Green,12,7.5f,12,16.5f); break;
            case "left": c.Line(p.Blue,20,12,4,12); c.Line(p.Blue,10,6,4,12,10,18); break;
            case "right": c.Line(p.Blue,4,12,20,12); c.Line(p.Blue,14,6,20,12,14,18); break;
            case "up": c.Line(p.Blue,12,20,12,4); c.Line(p.Blue,6,10,12,4,18,10); break;
            case "exec":
                c.Box(p.Blue,3,4,18,16,2,true); c.Line(p.Ink,7,9,10,12,7,15); c.Line(p.Green,13,15,17,15); break;
            case "advanced":
                c.Line(p.Muted,5,3,5,21); c.Line(p.Muted,12,3,12,21); c.Line(p.Muted,19,3,19,21);
                c.Box(p.Blue,3,7,4,4,1,true); c.Box(p.Violet,10,14,4,4,1,true); c.Box(p.Blue,17,5,4,4,1,true); break;
            case "styles":
                c.Box(p.Blue,3,4,6,16,1.5f,true); c.Box(p.Violet,9,7,6,13,1.5f,true); c.Box(p.Gold,15,10,6,10,1.5f,true);
                c.Dot(p.Ink,6,16.5f,.7f); c.Dot(p.Ink,12,16.5f,.7f); c.Dot(p.Ink,18,16.5f,.7f); break;
            case "bandwidthlimiter": Gauge(c,0); break;
            case "ulimit": Gauge(c,1); break;
            case "dlimit": Gauge(c,-1); break;
            case "connblue": case "conngrey":
                Color wifi=name=="connblue" ? p.Blue : p.Muted;
                c.Arc(wifi,2,5,20,20,225,90); c.Arc(wifi,6,10,12,12,225,90); c.Dot(wifi,12,19,1.5f);
                if(name=="conngrey") c.Line(p.Muted,4,21,20,5); break;
            case "expert":
                c.Polygon(p.Violet,true,14,3,12,8,16,12,21,10,21,15,17,18,13,17,7,21,3.5f,17.5f,9,12,8,8,10,4);
                if(!c.Small) c.Dot(p.Ink,6,18.5f,.75f); break;
            case "notifications":
                c.Arc(p.Gold,6,4,12,14,180,180); c.Line(p.Gold,6,11,5,16,3,18,21,18,19,16,18,11);
                c.Arc(p.Ink,10,17,4,4,0,180); c.Dot(p.Gold,12,3,1); break;
            case "proxy":
                c.Line(p.Muted,6,12,12,12,18,5); c.Line(p.Muted,12,12,18,19);
                c.Circle(p.Blue,5.5f,12,2.5f,true); c.Circle(p.Violet,18.5f,5,2.5f,true); c.Circle(p.Violet,18.5f,19,2.5f,true); break;
            case "tabs":
                c.Box(p.Blue,3,5,18,15,1.5f,true); c.Line(p.Blue,3,10,21,10);
                c.Box(p.Violet,5,3,7,7,1,true); break;
            case "windows":
                c.Box(p.Muted,3,8,13,13,1.5f,true); c.Box(p.Blue,8,3,13,13,1.5f,true); c.Line(p.Blue,8,7,21,7); break;
            case "sound": Sound(c,false); break;
            case "plugins":
                c.Line(p.Violet,8,3,8,8); c.Line(p.Violet,16,3,16,8); c.Box(p.Violet,5,8,14,9,3,true);
                c.Line(p.Violet,12,17,12,21); c.Line(p.Ink,9,12,15,12); break;
            case "uploadfiltering":
                c.Polygon(p.Blue,true,3,4,21,4,14,12,14,19,10,21,10,12); c.Badge("up",p.Violet); break;
            case "remove":
                c.Box(p.Red,6,7,12,14,1.5f,true); c.Line(p.Red,4,7,20,7); c.Line(p.Red,9,3,15,3);
                c.Line(p.Ink,10,11,10,17); c.Line(p.Ink,14,11,14,17); break;
            case "pause": c.Box(p.Gold,5,4,4,16,1,true); c.Box(p.Gold,15,4,4,16,1,true); break;
            case "play": c.Polygon(p.Green,true,6,3,21,12,6,21); break;
            case "warning":
                c.Polygon(p.Gold,true,12,3,21,21,3,21); c.Line(p.Gold,12,9,12,14); c.Dot(p.Gold,12,17.5f,.9f); break;
            default: throw new InvalidDataException("No artwork designed for " + name);
        }
    }

    public static byte[] CreateIcon(string fileName, string theme) {
        return CreateIcon(size => Render(fileName,size,theme));
    }
    public static byte[] CreateIcon(Func<int,Bitmap> render) {
        var frames=new List<byte[]>();
        foreach(int size in Sizes) using(Bitmap bitmap=render(size)) frames.Add(Encode(bitmap,size==256));
        using(var stream=new MemoryStream()) using(var writer=new BinaryWriter(stream)) {
            writer.Write((ushort)0); writer.Write((ushort)1); writer.Write((ushort)Sizes.Length);
            int offset=6+Sizes.Length*16;
            for(int i=0;i<Sizes.Length;i++) {
                writer.Write((byte)(Sizes[i]==256 ? 0 : Sizes[i])); writer.Write((byte)(Sizes[i]==256 ? 0 : Sizes[i]));
                writer.Write((byte)0); writer.Write((byte)0); writer.Write((ushort)1); writer.Write((ushort)32);
                writer.Write(frames[i].Length); writer.Write(offset); offset+=frames[i].Length;
            }
            foreach(byte[] frame in frames) writer.Write(frame);
            return stream.ToArray();
        }
    }
    private static byte[] Encode(Bitmap bitmap, bool png) {
        using(var stream=new MemoryStream()) {
            if(png) bitmap.Save(stream,ImageFormat.Png);
            else using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true)) {
                int size=bitmap.Width, maskStride=((size+31)/32)*4;
                writer.Write(40); writer.Write(size); writer.Write(size*2); writer.Write((ushort)1); writer.Write((ushort)32);
                writer.Write(0); writer.Write(size*size*4); writer.Write(0); writer.Write(0); writer.Write(0); writer.Write(0);
                for(int y=size-1;y>=0;y--) for(int x=0;x<size;x++) {
                    Color pixel=bitmap.GetPixel(x,y);
                    // Straight BGRA, with zero RGB outside the antialiased silhouette.
                    writer.Write(pixel.A==0 ? (byte)0 : pixel.B); writer.Write(pixel.A==0 ? (byte)0 : pixel.G);
                    writer.Write(pixel.A==0 ? (byte)0 : pixel.R); writer.Write(pixel.A);
                }
                for(int y=size-1;y>=0;y--) {
                    byte[] mask=new byte[maskStride];
                    for(int x=0;x<size;x++) if(bitmap.GetPixel(x,y).A==0) mask[x/8]|=(byte)(0x80>>(x%8));
                    writer.Write(mask);
                }
            }
            return stream.ToArray();
        }
    }
}
