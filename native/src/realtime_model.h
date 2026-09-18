#pragma once
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <numeric>
#include <unordered_map>
#include <array>

namespace realtime {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D11 model rendering failed");}
struct Vertex {float x,y,z,u,v,r,g,b,kind;};
struct Pick {int kind=0;float u=0,v=0;};
inline int screenButton(float u,float v){
    const float x=u*84,y=v*48;
    if(y<19 || y>=34)return -2;
    for(int i=0;i<4;++i)if(x>=8+i*18 && x<22+i*18)return i;
    return -2;
}
class Model {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext4> fenceContext;
    ComPtr<ID3D11Fence> completionFence;
    HANDLE completionEvent=nullptr;
    PTP_WAIT completionWait=nullptr;
    HWND completionWindow=nullptr;
    UINT completionMessage=0;
    UINT64 completionValue=0;
    bool inFlight=false;
    float nextYaw=0,nextPitch=0,nextRoll=0,nextBob=0,nextLeft=0,nextRight=0;
    LARGE_INTEGER submittedAt{};
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> mesh,indices,constants;
    ComPtr<ID3D11Texture2D> target,resolved,staging,depth,terminal;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> atlasView,facesView,terminalView;
    ComPtr<ID3D11SamplerState> nearest,linear;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthState;
    std::vector<Vertex> vertices;
    UINT indexCount=0,vertexCount=0;
    struct Vec {float x,y,z;Vec operator-(const Vec& b)const{return {x-b.x,y-b.y,z-b.z};}Vec operator+(const Vec& b)const{return {x+b.x,y+b.y,z+b.z};}Vec operator*(float s)const{return {x*s,y*s,z*s};}};
    static Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    static float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
    static Vec position(const Vertex& a){return {a.x,a.y,a.z};}
    static Vec bend(Vec point,int group,float angle,bool direction=false){
        if(!group || angle==0)return point;
        const Vec pivot{group==1?-26.f:26.f,74.5f,5.f};
        if(!direction)point=point-pivot;const float c=cosf(angle),s=sinf(angle);const Vec rotated{point.x*c-point.y*s,point.x*s+point.y*c,point.z};return direction?rotated:rotated+pivot;
    }
    struct Bounds {Vec lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};void add(Vec v){lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}};
    struct Node {Bounds bounds;int left=-1,right=-1;size_t first=0,count=0;};
    std::vector<Node> hierarchy;
    std::array<int,3> roots{-1,-1,-1};
    std::array<std::vector<Vec>,2> antennaPoints;
    std::vector<size_t> triangles;
    mutable uint64_t pickTests=0;
    int buildNode(size_t first,size_t count){
        Node node;node.first=first;node.count=count;
        for(size_t j=first;j<first+count;++j)for(size_t k=0;k<3;++k)node.bounds.add(position(vertices[triangles[j]*3+k]));
        const int id=static_cast<int>(hierarchy.size());hierarchy.push_back(node);
        if(count>8){
            const Vec span=node.bounds.hi-node.bounds.lo;const int axis=span.x>span.y && span.x>span.z?0:span.y>span.z?1:2;
            auto center=[&](size_t t){Vec c=(position(vertices[t*3])+position(vertices[t*3+1])+position(vertices[t*3+2]))*(1.f/3);return axis==0?c.x:axis==1?c.y:c.z;};
            const size_t middle=first+count/2;
            std::nth_element(triangles.begin()+first,triangles.begin()+middle,triangles.begin()+first+count,[&](size_t a,size_t b){return center(a)<center(b);});
            const int left=buildNode(first,count/2),right=buildNode(middle,count-count/2);
            hierarchy[id].left=left;hierarchy[id].right=right;hierarchy[id].count=0;
        }
        return id;
    }
    static bool intersects(const Bounds& box,Vec origin,Vec direction,float limit){
        float entryDistance=0,exitDistance=limit;
        const float o[]={origin.x,origin.y,origin.z},d[]={direction.x,direction.y,direction.z},lo[]={box.lo.x,box.lo.y,box.lo.z},hi[]={box.hi.x,box.hi.y,box.hi.z};
        for(int i=0;i<3;++i){if(std::abs(d[i])<1e-8f){if(o[i]<lo[i] || o[i]>hi[i])return false;continue;}
            float a=(lo[i]-o[i])/d[i],b=(hi[i]-o[i])/d[i];if(a>b)std::swap(a,b);entryDistance=std::max(entryDistance,a);exitDistance=std::min(exitDistance,b);if(entryDistance>exitDistance)return false;}
        return true;
    }
    struct GPUVertex {Vertex vertex;float nx,ny,nz;};
    UINT size=0;
    float yaw=0,pitch=0,roll=0,bob=0,leftBend=0,rightBend=0;
    mutable int pickX=-9999,pickY=-9999,pickExtent=0;
    mutable Pick lastPick{};
    struct Params {float cy,sy,cp,sp,cr,sr,bob,face,console,button,material,pad1,leftBend,rightBend,pad2,pad3;};
    static constexpr const char* shader=R"(
cbuffer Params:register(b0){float cy,sy,cp,sp,cr,sr,bob,face,console,button,material,pad1,leftBend,rightBend,pad2,pad3;}
struct Input{float3 p:POSITION;float2 uv:TEXCOORD;float3 color:COLOR;float kind:TYPE;float3 n:NORMAL;};
struct Output{float4 p:SV_POSITION;float2 uv:TEXCOORD;float3 color:COLOR;nointerpolation float kind:TYPE;float3 surface:TEXCOORD1;float3 local:TEXCOORD2;nointerpolation float3 lighting:TEXCOORD3;};
Output vertex(Input a){Output o;float3 p=a.p;if(a.kind==2 && button==2)p.z-=.65;
float group=floor(a.kind/10);a.kind=fmod(a.kind,10);
if(group>0){float angle=group==1?leftBend:-rightBend;float2 pivot=float2(group==1?-26:26,74.5);float2 q=p.xy-pivot;float c=cos(angle),s=sin(angle);p.xy=pivot+float2(c*q.x-s*q.y,s*q.x+c*q.y);q=a.n.xy;a.n.xy=float2(c*q.x-s*q.y,s*q.x+c*q.y);}
float x=(cy*p.x-sy*(p.z+3))/60;
float y=(-sp*sy*p.x+cp*(p.y-44)-sp*cy*(p.z+3))/60;
float z=cp*sy*p.x+sp*(p.y-44)+cp*cy*(p.z+3);
o.p=float4(x*cr+y*sr,-x*sr+y*cr-bob, .5-z/400,1);o.uv=a.uv;o.color=a.color;o.kind=a.kind;o.surface=float3(x*60,y*60,z);o.local=a.p;
float3 n=float3(cy*a.n.x-sy*a.n.z,-sp*sy*a.n.x+cp*a.n.y-sp*cy*a.n.z,cp*sy*a.n.x+sp*a.n.y+cp*cy*a.n.z);
n*=n.z<0?-1:1;o.lighting=float3(saturate(dot(n,normalize(float3(-.4,.65,1)))),saturate(dot(n,normalize(float3(-.22,.34,1)))),abs(n.z));return o;}
Texture2D atlas:register(t0);Texture2D faces:register(t1);Texture2D terminal:register(t2);
SamplerState nearest:register(s0);SamplerState smooth:register(s1);
float4 pixel(Output a):SV_TARGET{
float4 c=atlas.Sample(nearest,a.uv)*float4(a.color,1);
if(a.kind==3 || a.kind==4){
float light=a.lighting.x,spec=a.lighting.y;
float choice=a.kind==4?1:material;
float grain=sin(a.local.y*48)*.5+.5;
float aa=saturate(1-length(fwidth(a.local))*3);
if(choice==1){
c.rgb=lerp(c.rgb,float3(.58,.68,.66),.38)*(.65+.35*light)+pow(spec,42)*.35;
c.rgb+=(grain-.5)*.026*aa;
}else if(choice==2){
float fresnel=pow(1-a.lighting.z,3);
float band=exp(-pow((a.surface.x+a.surface.y*.7-12)/9,2));
c.rgb=c.rgb*(.48+.24*light)+float3(.55,.83,.81)*(fresnel*.34+band*.18)+pow(spec,100)*.55;
}else if(choice==3){
float weave=sin(a.local.x*10)*sin(a.local.y*10);
c.rgb=lerp(c.rgb,float3(.79,.80,.73),.32)*(.82+.18*light)+(weave*.026*aa)+pow(spec,8)*.045;
}else{
c.rgb*=.8+.2*light;c.rgb+=pow(spec,18)*.095;
}}
if(a.kind==1){if(console>0)c=terminal.Sample(smooth,a.uv);else {uint w,h;faces.GetDimensions(w,h);float2 uv=clamp(a.uv,float2(6.0/w,.5/h),float2(1-6.0/w,1-.5/h));c=faces.Sample(nearest,float2((uv.x+face)/12,uv.y));}}
if(a.kind==1 && console==0){
float2 centered=a.uv*2-1;
c.rgb*=1-.018*saturate(dot(centered,centered)*.5);
}
if(a.kind==2){if(button==3)c.rgb=float3(.35,.95,.65);else if(button==2)c.rgb*=.65;else if(button==1)c.rgb=min(1,c.rgb*1.3+.08);}
return float4(c.rgb*c.a,c.a);}
)";
    void resize(UINT next){
        if(size==next)return;
        UINT levels=0;check(device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM,4,&levels));const UINT samples=levels?4:1;
        ComPtr<ID3D11Texture2D> newTarget,newResolved,newStaging,newDepth;ComPtr<ID3D11RenderTargetView> newRtv;ComPtr<ID3D11DepthStencilView> newDsv;
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=next;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=samples;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&d,nullptr,&newTarget));check(device->CreateRenderTargetView(newTarget.Get(),nullptr,&newRtv));
        d.SampleDesc.Count=1;d.BindFlags=0;check(device->CreateTexture2D(&d,nullptr,&newResolved));d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&d,nullptr,&newStaging));
        d.Format=DXGI_FORMAT_D32_FLOAT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;
        d.SampleDesc.Count=samples;check(device->CreateTexture2D(&d,nullptr,&newDepth));check(device->CreateDepthStencilView(newDepth.Get(),nullptr,&newDsv));
        // Build everything first: if any allocation fails the old targets stay usable, so size never
        // disagrees with the resources. Unbind before swapping so nothing is released while still bound.
        context->OMSetRenderTargets(0,nullptr,nullptr);
        target=newTarget;resolved=newResolved;staging=newStaging;depth=newDepth;rtv=newRtv;dsv=newDsv;size=next;
    }
    void texture(UINT w,UINT h,const uint32_t* pixels,ComPtr<ID3D11ShaderResourceView>& view){
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels,w*4,0};ComPtr<ID3D11Texture2D> tex;check(device->CreateTexture2D(&d,&data,&tex));check(device->CreateShaderResourceView(tex.Get(),nullptr,&view));
    }
public:
    ~Model(){disableAsync();}
    bool ready()const{return device!=nullptr;}
    UINT uniqueVertices()const{return vertexCount;}
    double submitMs=0,readbackMs=0,copyMs=0,gpuLatencyMs=0;
    uint64_t testedTriangles()const{return pickTests;}
    bool asyncReady()const{return completionWait!=nullptr;}
    bool pending()const{return inFlight;}
    void disableAsync(){
        discardPending();
        if(completionWait){SetThreadpoolWait(completionWait,nullptr,nullptr);WaitForThreadpoolWaitCallbacks(completionWait,TRUE);CloseThreadpoolWait(completionWait);completionWait=nullptr;}
        if(completionEvent){CloseHandle(completionEvent);completionEvent=nullptr;}
        completionFence.Reset();fenceContext.Reset();inFlight=false;
    }
    bool enableAsync(HWND window,UINT message){
        if(!IsWindow(window))return false;
        ComPtr<ID3D11Device5> latest;
        if(FAILED(device.As(&latest)) || FAILED(context.As(&fenceContext)) || FAILED(latest->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&completionFence)))){disableAsync();return false;}
        completionWindow=window;completionMessage=message;
        completionEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        completionWait=CreateThreadpoolWait([](PTP_CALLBACK_INSTANCE,void* data,PTP_WAIT,TP_WAIT_RESULT){const auto* model=static_cast<Model*>(data);PostMessageW(model->completionWindow,model->completionMessage,0,0);},this,nullptr);
        if(!completionEvent || !completionWait){disableAsync();return false;}return true;
    }
    void discardPending(){
        if(!inFlight)return;
        SetThreadpoolWait(completionWait,nullptr,nullptr);WaitForThreadpoolWaitCallbacks(completionWait,TRUE);
        if(completionFence->GetCompletedValue()<completionValue){check(completionFence->SetEventOnCompletion(completionValue,completionEvent));WaitForSingleObject(completionEvent,INFINITE);}
        ResetEvent(completionEvent);inFlight=false;
    }
    bool readPixels(uint32_t* output,int extent){
        if(inFlight && completionFence->GetCompletedValue()<completionValue)return false;
        LARGE_INTEGER started{},mappedAt{},copied{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&started);
        gpuLatencyMs=1000.0*(started.QuadPart-submittedAt.QuadPart)/frequency.QuadPart;
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));QueryPerformanceCounter(&mappedAt);
        for(int y=0;y<extent;++y){const auto* row=reinterpret_cast<const uint32_t*>(static_cast<const char*>(mapped.pData)+(static_cast<size_t>(y)*size/extent)*mapped.RowPitch);
            if(size==static_cast<UINT>(extent))memcpy(output+static_cast<size_t>(y)*extent,row,extent*4);else for(int x=0;x<extent;++x)output[static_cast<size_t>(y)*extent+x]=row[static_cast<size_t>(x)*size/extent];}
        context->Unmap(staging.Get(),0);QueryPerformanceCounter(&copied);
        readbackMs=1000.0*(mappedAt.QuadPart-started.QuadPart)/frequency.QuadPart;copyMs=1000.0*(copied.QuadPart-mappedAt.QuadPart)/frequency.QuadPart;
        yaw=nextYaw;pitch=nextPitch;roll=nextRoll;bob=nextBob;leftBend=nextLeft;rightBend=nextRight;pickExtent=0;inFlight=false;return true;
    }
    void initialize(const void* data,size_t bytes,const uint32_t* atlas,UINT aw,UINT ah,const uint32_t* faces,UINT fw,UINT fh){
        UINT count=0;if(bytes<4)throw std::runtime_error("Invalid model asset");memcpy(&count,data,4);
        if(count%3 || bytes!=4+static_cast<size_t>(count)*sizeof(Vertex))throw std::runtime_error("Invalid model vertex data");
        vertices.resize(count);memcpy(vertices.data(),static_cast<const char*>(data)+4,bytes-4);
        triangles.clear();hierarchy.clear();hierarchy.reserve(count/3);
        for(int group=0;group<3;++group){const size_t first=triangles.size();for(size_t i=0;i<count/3;++i)if(static_cast<int>(vertices[i*3].kind)/10==group)triangles.push_back(i);roots[group]=triangles.size()>first?buildNode(first,triangles.size()-first):-1;}
        for(int group=1;group<3;++group){auto& points=antennaPoints[group-1];points.clear();
            for(const auto& vertex:vertices)if(static_cast<int>(vertex.kind)/10==group)points.push_back(position(vertex));
            std::sort(points.begin(),points.end(),[](Vec a,Vec b){return a.x!=b.x?a.x<b.x:a.y!=b.y?a.y<b.y:a.z<b.z;});
            points.erase(std::unique(points.begin(),points.end(),[](Vec a,Vec b){return a.x==b.x && a.y==b.y && a.z==b.z;}),points.end());}
        struct Hash {size_t operator()(const std::array<uint32_t,12>& key)const{size_t h=2166136261u;for(auto v:key)h=(h^v)*16777619u;return h;}};
        std::unordered_map<std::array<uint32_t,12>,uint32_t,Hash> unique;
        std::vector<GPUVertex> gpuVertices;std::vector<uint32_t> gpuIndices;gpuIndices.reserve(count);
        for(size_t i=0;i<vertices.size();i+=3){Vec n{};
            if(static_cast<int>(vertices[i].kind)%10==3 || static_cast<int>(vertices[i].kind)%10==4){n=cross(position(vertices[i+1])-position(vertices[i]),position(vertices[i+2])-position(vertices[i]));const float length=std::sqrt(dot(n,n));n=n*(length>1e-10f?1/length:0);
                // Share coplanar faces despite insignificant float rounding in the packed mesh.
                n={std::round(n.x*10000)/10000,std::round(n.y*10000)/10000,std::round(n.z*10000)/10000};
                if(n.x==0)n.x=0;if(n.y==0)n.y=0;if(n.z==0)n.z=0;}
            for(size_t j=0;j<3;++j){GPUVertex v{vertices[i+j],n.x,n.y,n.z};std::array<uint32_t,12> key{};memcpy(key.data(),&v,sizeof(v));
                auto [entry,added]=unique.emplace(key,static_cast<uint32_t>(gpuVertices.size()));if(added)gpuVertices.push_back(v);gpuIndices.push_back(entry->second);}}
        vertexCount=static_cast<UINT>(gpuVertices.size());indexCount=count;
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<ID3DBlob> v,p,errors;
        check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vertex","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&errors));
        check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"pixel","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&errors));
        check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},{"TYPE",0,DXGI_FORMAT_R32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
        D3D11_INPUT_ELEMENT_DESC gpuElements[5]{};std::copy_n(elements,4,gpuElements);gpuElements[4]={"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0};
        check(device->CreateInputLayout(gpuElements,5,v->GetBufferPointer(),v->GetBufferSize(),&layout));
        D3D11_BUFFER_DESC b{};b.ByteWidth=vertexCount*sizeof(GPUVertex);b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.Usage=D3D11_USAGE_IMMUTABLE;D3D11_SUBRESOURCE_DATA initial{gpuVertices.data(),0,0};check(device->CreateBuffer(&b,&initial,&mesh));
        b.ByteWidth=count*sizeof(uint32_t);b.BindFlags=D3D11_BIND_INDEX_BUFFER;initial.pSysMem=gpuIndices.data();check(device->CreateBuffer(&b,&initial,&indices));
        b.ByteWidth=sizeof(Params);b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.Usage=D3D11_USAGE_DEFAULT;check(device->CreateBuffer(&b,nullptr,&constants));
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_BACK;r.FrontCounterClockwise=TRUE;r.DepthClipEnable=TRUE;r.MultisampleEnable=TRUE;check(device->CreateRasterizerState(&r,&raster));
        D3D11_DEPTH_STENCIL_DESC depthDescription{};depthDescription.DepthEnable=TRUE;depthDescription.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depthDescription.DepthFunc=D3D11_COMPARISON_LESS;
        check(device->CreateDepthStencilState(&depthDescription,&depthState));
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;check(device->CreateSamplerState(&s,&nearest));s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;check(device->CreateSamplerState(&s,&linear));
        texture(aw,ah,atlas,atlasView);texture(fw,fh,faces,facesView);
        D3D11_TEXTURE2D_DESC t{};t.Width=800;t.Height=500;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_B8G8R8A8_UNORM;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&t,nullptr,&terminal));check(device->CreateShaderResourceView(terminal.Get(),nullptr,&terminalView));
    }
    void uploadTerminal(const uint32_t* pixels){context->UpdateSubresource(terminal.Get(),0,nullptr,pixels,800*4,0);}
    bool draw(uint32_t* output,int extent,double angle,double elevation,double tilt,int offset,bool hd,int face,int button,bool console,int material=0,bool asynchronous=false,float antennaLeft=0,float antennaRight=0){
        LARGE_INTEGER started{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&started);
        nextYaw=static_cast<float>(angle*3.141592653589793/180);nextPitch=static_cast<float>(elevation*3.141592653589793/180);nextRoll=static_cast<float>(tilt*3.141592653589793/180);nextBob=static_cast<float>(offset)/96;
        nextLeft=std::clamp(antennaLeft,0.f,1.15f);nextRight=std::clamp(antennaRight,0.f,1.15f);
        resize(static_cast<UINT>(hd?extent:192));
        Params parameters{cosf(nextYaw),sinf(nextYaw),cosf(nextPitch),sinf(nextPitch),cosf(nextRoll),sinf(nextRoll),nextBob,static_cast<float>(face),console?1.f:0.f,static_cast<float>(button),static_cast<float>(material),0,nextLeft,nextRight,0,0};
        context->UpdateSubresource(constants.Get(),0,nullptr,&parameters,0,0);
        const float clear[4]{};context->ClearRenderTargetView(rtv.Get(),clear);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
        ID3D11RenderTargetView* render=rtv.Get();context->OMSetRenderTargets(1,&render,dsv.Get());context->OMSetDepthStencilState(depthState.Get(),0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(size),static_cast<float>(size),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        UINT stride=sizeof(GPUVertex),start=0;ID3D11Buffer* buffer=mesh.Get();context->IASetVertexBuffers(0,1,&buffer,&stride,&start);context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R32_UINT,0);context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);buffer=constants.Get();context->VSSetConstantBuffers(0,1,&buffer);context->PSSetConstantBuffers(0,1,&buffer);
        ID3D11ShaderResourceView* views[]={atlasView.Get(),facesView.Get(),terminalView.Get()};context->PSSetShaderResources(0,3,views);ID3D11SamplerState* samplers[]={nearest.Get(),linear.Get()};context->PSSetSamplers(0,2,samplers);
        context->DrawIndexed(indexCount,0,0);D3D11_TEXTURE2D_DESC description{};target->GetDesc(&description);if(description.SampleDesc.Count>1){context->ResolveSubresource(resolved.Get(),0,target.Get(),0,DXGI_FORMAT_B8G8R8A8_UNORM);context->CopyResource(staging.Get(),resolved.Get());}else context->CopyResource(staging.Get(),target.Get());
        if(asynchronous && asyncReady()){
            ResetEvent(completionEvent);check(fenceContext->Signal(completionFence.Get(),++completionValue));
            check(completionFence->SetEventOnCompletion(completionValue,completionEvent));inFlight=true;
            SetThreadpoolWait(completionWait,completionEvent,nullptr);context->Flush();
        }
        QueryPerformanceCounter(&submittedAt);submitMs=1000.0*(submittedAt.QuadPart-started.QuadPart)/frequency.QuadPart;
        return inFlight?false:readPixels(output,extent);
    }
    RECT groupBounds(int extent,int group,float fold,float viewYaw,float viewPitch,float viewRoll,float viewBob)const{
        if(roots[group]<0)return {};
        const auto& bounds=hierarchy[roots[group]].bounds;
        const float cy=cosf(viewYaw),sy=sinf(viewYaw),cp=cosf(viewPitch),sp=sinf(viewPitch),cr=cosf(viewRoll),sr=sinf(viewRoll);
        float left=static_cast<float>(extent),top=left,right=0,bottom=0;
        const float c=cosf(fold),s=sinf(group==1?fold:-fold),pivotX=group==1?-26.f:26.f;
        auto project=[&](Vec point){
            const float x=group?pivotX+c*(point.x-pivotX)-s*(point.y-74.5f):point.x;
            const float y=group?74.5f+s*(point.x-pivotX)+c*(point.y-74.5f):point.y,z=point.z;
            const float xx=(cy*x-sy*(z+3))/60,yy=(-sp*sy*x+cp*(y-44)-sp*cy*(z+3))/60;
            const float px=(1+xx*cr+yy*sr)*extent*.5f,py=(1+xx*sr-yy*cr+viewBob)*extent*.5f;
            left=std::min(left,px);right=std::max(right,px);top=std::min(top,py);bottom=std::max(bottom,py);};
        // Only the small articulated groups need their exact silhouette; the body uses eight corners.
        if(group)for(const auto& point:antennaPoints[group-1])project(point);
        else for(int i=0;i<8;++i)project({i&1?bounds.hi.x:bounds.lo.x,i&2?bounds.hi.y:bounds.lo.y,i&4?bounds.hi.z:bounds.lo.z});
        const LONG padding=std::max(2,(extent+191)/192);
        return {std::clamp(static_cast<LONG>(std::floor(left))-padding,0L,static_cast<LONG>(extent)),std::clamp(static_cast<LONG>(std::floor(top))-padding,0L,static_cast<LONG>(extent)),
            std::clamp(static_cast<LONG>(std::ceil(right))+padding,0L,static_cast<LONG>(extent)),std::clamp(static_cast<LONG>(std::ceil(bottom))+padding,0L,static_cast<LONG>(extent))};
    }
    RECT visibleBounds(int extent)const{
        if(hierarchy.empty())return {0,0,extent,extent};
        auto bounds=groupBounds(extent,0,0,yaw,pitch,roll,bob);
        for(int group=1;group<3;++group){const auto part=groupBounds(extent,group,group==1?leftBend:rightBend,yaw,pitch,roll,bob);UnionRect(&bounds,&bounds,&part);}return bounds;
    }
    RECT compressionBounds(int extent,double angle,double elevation,double tilt,int offset)const{
        constexpr float radians=3.141592653589793f/180;
        const float y=static_cast<float>(angle)*radians,p=static_cast<float>(elevation)*radians,r=static_cast<float>(tilt)*radians,b=offset/96.f;
        auto bounds=groupBounds(extent,0,0,y,p,r,b);
        for(int group=1;group<3;++group){const auto folded=groupBounds(extent,group,1.15f,y,p,r,b);UnionRect(&bounds,&bounds,&folded);}
        // Reserve lateral sweep, but let upward travel compress the antennas to their hinges.
        const LONG top=bounds.top;
        for(int group=1;group<3;++group)for(float fold:{0.f,.575f}){const auto part=groupBounds(extent,group,fold,y,p,r,b);UnionRect(&bounds,&bounds,&part);}
        bounds.top=top;return bounds;
    }
    std::array<float,2> antennaContact(int extent,LONG ceiling,double angle,double elevation,double tilt,int offset)const{
        constexpr float radians=3.141592653589793f/180;
        const float y=static_cast<float>(angle)*radians,p=static_cast<float>(elevation)*radians,r=static_cast<float>(tilt)*radians,b=offset/96.f;
        std::array<float,2> result{};
        for(int group=1;group<3;++group){
            if(roots[group]<0 || groupBounds(extent,group,0,y,p,r,b).top>=ceiling)continue;
            float low=0,high=1.15f;
            for(int i=1;i<=16;++i){high=1.15f*i/16;if(groupBounds(extent,group,high,y,p,r,b).top>=ceiling)break;low=high;}
            for(int i=0;i<10 && high>low;++i){const float middle=(low+high)*.5f;if(groupBounds(extent,group,middle,y,p,r,b).top>=ceiling)high=middle;else low=middle;}
            result[group-1]=high;
        }return result;
    }
    Pick pickReference(int x,int y,int extent)const{
        if(x<0 || y<0 || x>=extent || y>=extent)return {};
        Pick hit{};float closest=-1e9f;const float px=2.f*x/extent-1,py=1-2.f*y/extent;
        const float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch),cr=cosf(roll),sr=sinf(roll);
        struct Point{float x,y,z,u,v;};
        auto project=[&](const Vertex& a){const int group=static_cast<int>(a.kind)/10;const auto p=bend(position(a),group,group==1?leftBend:-rightBend);float xx=(cy*p.x-sy*(p.z+3))/60,yy=(-sp*sy*p.x+cp*(p.y-44)-sp*cy*(p.z+3))/60;return Point{xx*cr+yy*sr,-xx*sr+yy*cr-bob,cp*sy*p.x+sp*(p.y-44)+cp*cy*(p.z+3),a.u,a.v};};
        for(size_t i=0;i<vertices.size();i+=3){const auto a=project(vertices[i]),b=project(vertices[i+1]),c=project(vertices[i+2]);
            if(px<std::min({a.x,b.x,c.x}) || px>std::max({a.x,b.x,c.x}) || py<std::min({a.y,b.y,c.y}) || py>std::max({a.y,b.y,c.y}))continue;
            const float d=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(d<1e-9f)continue;
            const float wa=((b.y-c.y)*(px-c.x)+(c.x-b.x)*(py-c.y))/d,wb=((c.y-a.y)*(px-c.x)+(a.x-c.x)*(py-c.y))/d,wc=1-wa-wb;
            if(wa<0 || wb<0 || wc<0)continue;const float z=wa*a.z+wb*b.z+wc*c.z;if(z>closest){closest=z;hit={static_cast<int>(vertices[i].kind)%10,wa*a.u+wb*b.u+wc*c.u,wa*a.v+wb*b.v+wc*c.v};}}
        return hit;
    }
    Pick pick(int x,int y,int extent)const{
        if(x<0 || y<0 || x>=extent || y>=extent)return {};
        if(pickX==x && pickY==y && pickExtent==extent)return lastPick;
        const float px=2.f*x/extent-1,py=1-2.f*y/extent+bob,cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch),cr=cosf(roll),sr=sinf(roll);
        const float xx=(px*cr-py*sr)*60,yy=(px*sr+py*cr)*60;
        const Vec rayOrigin{cy*xx-sp*sy*yy+cp*sy*200,44+cp*yy+sp*200,-3-sy*xx-sp*cy*yy+cp*cy*200};
        const Vec rayDirection{-cp*sy,-sp,-cp*cy};float closest=400;Pick hit{};
        for(int group=0;group<3;++group){if(roots[group]<0)continue;
        const float inverse=group==1?-leftBend:rightBend;const Vec origin=bend(rayOrigin,group,inverse),direction=bend(rayDirection,group,inverse,true);
        std::array<int,64> stack{};stack[0]=roots[group];size_t pending=1;
        while(pending){const auto& node=hierarchy[stack[--pending]];if(!intersects(node.bounds,origin,direction,closest))continue;
            if(!node.count){stack[pending++]=node.left;stack[pending++]=node.right;continue;}
            for(size_t j=node.first;j<node.first+node.count;++j){++pickTests;const size_t i=triangles[j]*3;
                const Vec a=position(vertices[i]),e1=position(vertices[i+1])-a,e2=position(vertices[i+2])-a,p=cross(direction,e2);const float determinant=dot(e1,p);
                if(determinant<1e-8f)continue;const Vec t=origin-a;const float u=dot(t,p)/determinant;if(u<0 || u>1)continue;
                const Vec q=cross(t,e1);const float v=dot(direction,q)/determinant;if(v<0 || u+v>1)continue;
                const float distance=dot(e2,q)/determinant;if(distance<0 || distance>=closest)continue;closest=distance;
                hit={static_cast<int>(vertices[i].kind)%10,(1-u-v)*vertices[i].u+u*vertices[i+1].u+v*vertices[i+2].u,(1-u-v)*vertices[i].v+u*vertices[i+1].v+v*vertices[i+2].v};
            }
        }
        }
        pickX=x;pickY=y;pickExtent=extent;lastPick=hit;return hit;
    }
};
}
