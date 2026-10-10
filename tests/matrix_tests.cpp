#include "Matrix.h"
#include "TrackingMath.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace bvr;

static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a-b) < e; }

static Mat4 Pose(float yaw, float pitch, float roll, float x, float y, float z) {
    Mat4 ry=Identity(), rx=Identity(), rz=Identity();
    ry.m[0][0]=ry.m[2][2]=std::cos(yaw);
    ry.m[0][2]=std::sin(yaw); ry.m[2][0]=-std::sin(yaw);
    rx.m[1][1]=rx.m[2][2]=std::cos(pitch);
    rx.m[2][1]=std::sin(pitch); rx.m[1][2]=-std::sin(pitch);
    rz.m[0][0]=rz.m[1][1]=std::cos(roll);
    rz.m[1][0]=std::sin(roll); rz.m[0][1]=-std::sin(roll);
    Mat4 p=Mul(ry,Mul(rx,rz));
    p.m[0][3]=x; p.m[1][3]=y; p.m[2][3]=z;
    return p;
}

static bool TrackingTests() {
    // Nontrivial recenter orientation catches world/local translation mixups.
    const Mat4 origin=Pose(0.63f,-0.21f,0.17f,1.2f,1.6f,-0.7f);
    float worstCenter=0, worstPixels=0;
    int cases=0;
    for (int yaw=-4;yaw<=4;++yaw) for (int pitch=-2;pitch<=2;++pitch)
    for (int roll=-2;roll<=2;++roll) for (int e=0;e<2;++e) {
        Mat4 eye=Pose(0,0,0,e ? 0.0313f : -0.0313f,0.004f,0.012f);
        const Mat4 head=Mul(origin,Pose(yaw*0.19f,pitch*0.13f,roll*0.11f,
                                      yaw*0.06f,pitch*0.09f,roll*0.08f));
        const Mat4 synthetic=CompositorRotationHead(origin,head,eye);
        const Mat4 renderEye=Mul(synthetic,eye), targetEye=Mul(head,eye);
        for(int r=0;r<3;++r) {
            const float delta=std::fabs(renderEye.m[r][3]-targetEye.m[r][3]);
            if(delta>worstCenter) worstCenter=delta;
            if(delta>2e-6f) return false;
            for(int c=0;c<3;++c)
                if(!Near(synthetic.m[r][c],origin.m[r][c])) return false;
        }
        // Model ONLY rotation reprojection: no depth/translation correction.
        Mat4 correction=Mul(InvertRigid(targetEye),renderEye);
        for(int r=0;r<3;++r) correction.m[r][3]=0;
        Mat4 flip=Identity(); flip.m[2][2]=-1;
        const Mat4 p=Mul(PerspectiveRawLH(e ? -0.8391f : -1.3764f,
                                         e ? 1.3764f : 0.8391f,
                                         -1.4281f,0.9657f,0.1f,10000),flip);
        const Mat4 native=Mul(p,Mul(InvertRigid(targetEye),origin));
        const Mat4 composed=Mul(p,Mul(correction,Mul(InvertRigid(renderEye),origin)));
        for(float depth : {0.4f,2.0f,20.0f}) {
            const float v[4]={0.07f,-0.03f,-depth,1};
            float a[4]{}, b[4]{};
            for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
                a[r]+=native.m[r][c]*v[c]; b[r]+=composed.m[r][c]*v[c];
            }
            if(std::fabs(a[3])<0.05f || std::fabs(b[3])<0.05f) continue;
            for(int r=0;r<2;++r) {
                const float error=std::fabs(a[r]/a[3]-b[r]/b[3])*1280;
                if(error>worstPixels) worstPixels=error;
                if(error>0.08f) return false;
            }
        }
        ++cases;
    }
    // At recenter, both eyes must be exactly the old fixed baseline.
    const Mat4 eye=Pose(0,0,0,0.0313f,0,0);
    const Mat4 baseline=CompositorRotationHead(origin,origin,eye);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        if(!Near(baseline.m[r][c],origin.m[r][c])) return false;
    // A 90-degree head turn moves each eye even with zero head translation.
    const Mat4 turned=Pose(1.5707963f,0,0,0,0,0);
    const Mat4 compensated=CompositorRotationHead(Identity(),turned,eye);
    if(!Near(compensated.m[0][3],-0.0313f) || !Near(compensated.m[2][3],-0.0313f)) return false;
    std::printf("tracking: %d stereo pose cases, max eye-center error %.9fm, max reprojection error %.6fpx\n",
                cases,worstCenter,worstPixels);
    return true;
}

static bool AnchorTests() {
    const Mat4 origin=Pose(0.63f,-0.21f,0.17f,1.2f,1.6f,-0.7f);
    int totalTurns=0, samples=0;
    float worstCenter=0, worstResidual=0;
    for(int axis=0;axis<3;++axis) {
        Mat4 anchor=origin;
        for(int degrees=-720;degrees<=720;++degrees) {
            const float angle=degrees*3.14159265358979323846f/180.0f;
            const Mat4 head=Mul(origin,Pose(axis==0 ? angle : 0.17f,
                axis==1 ? angle : -0.11f,axis==2 ? angle : 0.23f,
                0.15f*std::sin(angle),0.09f*std::cos(angle),-0.07f));
            const Mat4 oldAnchor=anchor;
            const bool turned=FollowRenderAnchor(anchor,head,12.0f);
            if(turned) ++totalTurns;
            if(RotationDistanceDegrees(anchor,head)>12.001f) return false;
            // Following direction must not move the anchor position.
            for(int row=0;row<3;++row)
                if(anchor.m[row][3]!=origin.m[row][3]) return false;
            for(int eyeIndex=0;eyeIndex<2;++eyeIndex) {
                // Include canted eyes as well as an offset in Y and Z.
                const Mat4 eye=Pose(eyeIndex ? 0.035f : -0.035f,0,0,
                    eyeIndex ? 0.0313f : -0.0313f,0.004f,0.012f);
                const Mat4 targetEye=Mul(head,eye);
                Mat4 previousView{};
                for(int pass=0;pass<2;++pass) {
                    const Mat4 renderEye=Mul(CompositorRotationHead(
                        pass ? anchor : oldAnchor,head,eye),eye);
                    for(int row=0;row<3;++row) {
                        const float delta=std::fabs(renderEye.m[row][3]-targetEye.m[row][3]);
                        if(delta>worstCenter) worstCenter=delta;
                        if(delta>2e-6f) return false;
                    }
                    Mat4 correction=Mul(InvertRigid(targetEye),renderEye);
                    for(int row=0;row<3;++row) correction.m[row][3]=0;
                    const Mat4 finalView=Mul(correction,Mul(InvertRigid(renderEye),origin));
                    const Mat4 expected=Mul(InvertRigid(targetEye),origin);
                    for(int row=0;row<4;++row) for(int col=0;col<4;++col) {
                        const float error=std::fabs(finalView.m[row][col]-expected.m[row][col]);
                        if(error>worstResidual) worstResidual=error;
                        if(error>4e-6f) return false;
                        if(pass && !Near(finalView.m[row][col],previousView.m[row][col],4e-6f)) return false;
                    }
                    previousView=finalView;
                }
            }
            ++samples;
        }
    }
    // Translation and small angular noise must leave the basis stable.
    Mat4 anchor=origin;
    for(int i=0;i<100;++i) {
        const Mat4 head=Mul(origin,Pose((i&1) ? 0.05f : -0.05f,0,0,i*0.01f,0,0));
        if(FollowRenderAnchor(anchor,head,12.0f)) return false;
    }
    if(FollowRenderAnchor(anchor,Pose(3.0f,0,0,0,0,0),0.0f)) return false;
    if(totalTurns<100 || totalTurns>samples/5) return false;
    std::printf("anchor: %d turn samples (yaw/pitch/roll +/-720deg), %d updates, max center error %.9fm, continuity residual %.9f\n",
                samples,totalTurns,worstCenter,worstResidual);
    return true;
}

int main() {
    Mat4 rigid = Identity();
    rigid.m[0][0]=0; rigid.m[0][2]=1; rigid.m[2][0]=-1; rigid.m[2][2]=0;
    rigid.m[0][3]=3; rigid.m[1][3]=-2; rigid.m[2][3]=7;
    Mat4 ri = InvertRigid(rigid);
    Mat4 unit = Mul(rigid,ri);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        if(!Near(unit.m[r][c],r==c?1.0f:0.0f)) return 1;

    Mat4 p = PerspectiveFovLH(1.1f, 16.0f/9.0f, 0.1f, 1000.0f), inv{};
    if(!Invert(p,inv)) return 2;
    unit=Mul(p,inv);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        if(!Near(unit.m[r][c],r==c?1.0f:0.0f)) return 3;

    Mat4 raw=PerspectiveRawLH(-1,1,-1,1,0.1f,1000.0f);
    if(!Near(raw.m[0][0],1)||!Near(raw.m[1][1],1)||!Near(raw.m[3][2],1)) return 4;
    if(!TrackingTests()) { std::puts("tracking tests FAILED"); return 5; }
    if(!AnchorTests()) { std::puts("anchor tests FAILED"); return 6; }
    std::puts("matrix and tracking tests passed");
    return 0;
}
