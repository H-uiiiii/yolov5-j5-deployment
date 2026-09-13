#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <map>
#include <dirent.h>
#include <opencv2/opencv.hpp>
#include "hb_dnn.h"
#include "hb_sys.h"

struct DetectBox { float x1,y1,x2,y2,conf; int class_id; };

const char* class_names[] = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck","boat",
    "traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat",
    "dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack",
    "umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball",
    "kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket",
    "bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple",
    "sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake",
    "chair","couch","potted plant","bed","dining table","toilet","tv","laptop",
    "mouse","remote","keyboard","cell phone","microwave","oven","toaster","sink",
    "refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"
};

float sigmoid(float x){return 1.0f/(1.0f+expf(-x));}
float calc_iou(const DetectBox& a,const DetectBox& b){
    float x1=std::max(a.x1,b.x1),y1=std::max(a.y1,b.y1);
    float x2=std::min(a.x2,b.x2),y2=std::min(a.y2,b.y2);
    float w=std::max(0.0f,x2-x1),h=std::max(0.0f,y2-y1);
    return w*h/((a.x2-a.x1)*(a.y2-a.y1)+(b.x2-b.x1)*(b.y2-b.y1)-w*h+1e-6f);
}
std::vector<DetectBox> nms(std::vector<DetectBox>& boxes,float thresh){
    std::sort(boxes.begin(),boxes.end(),[](const DetectBox&a,const DetectBox&b){return a.conf>b.conf;});
    std::vector<DetectBox> res; std::vector<bool> sup(boxes.size(),false);
    for(size_t i=0;i<boxes.size();i++){if(sup[i])continue;res.push_back(boxes[i]);
        for(size_t j=i+1;j<boxes.size();j++){if(!sup[j]&&boxes[i].class_id==boxes[j].class_id&&calc_iou(boxes[i],boxes[j])>thresh)sup[j]=true;}}
    return res;
}

int main(){
    const char* model_path="/workspace/model_output/yolov5su_j5.bin";
    const char* test_dir="/workspace/dataset/test/";
    system("mkdir -p /workspace/infer/results");

    hbPackedDNNHandle_t packed=nullptr;
    hbDNNInitializeFromFiles(&packed,&model_path,1);
    char const** nl=nullptr; int32_t nc=0;
    hbDNNGetModelNameList(&nl,&nc,packed);
    hbDNNHandle_t dnn=nullptr;
    hbDNNGetModelHandle(&dnn,packed,nl[0]);

    hbDNNTensorProperties iprop,oprop;
    hbDNNGetInputTensorProperties(&iprop,dnn,0);
    hbDNNGetOutputTensorProperties(&oprop,dnn,0);
    hbSysMem imem,omem;
    hbSysAllocCachedMem(&imem,iprop.alignedByteSize);
    hbSysAllocCachedMem(&omem,oprop.alignedByteSize);
    hbDNNTensor itensor,otensor;
    itensor.properties=iprop; itensor.sysMem[0]=imem;
    otensor.properties=oprop; otensor.sysMem[0]=omem;

    std::vector<std::string> files;
    DIR* d=opendir(test_dir); struct dirent* e;
    while((e=readdir(d))){std::string f=e->d_name;
        if(f.find(".jpg")!=std::string::npos)files.push_back(f);}
    closedir(d); std::sort(files.begin(),files.end());

    int total_det=0; float total_time=0;
    std::map<std::string,int> cls_cnt;
    std::vector<std::string> empty_imgs;

    std::cout<<"开始批量测试，共"<<files.size()<<"张图片..."<<std::endl;
    std::cout<<"========================================"<<std::endl;

    for(size_t idx=0;idx<files.size();idx++){
        std::string ipath=std::string(test_dir)+files[idx];
        cv::Mat img=cv::imread(ipath);
        if(img.empty()){std::cout<<"["<<idx+1<<"/"<<files.size()<<"] "<<files[idx]<<" 跳过"<<std::endl;continue;}
        cv::Mat rsz; cv::resize(img,rsz,cv::Size(640,640));
        uint8_t* idata=(uint8_t*)imem.virAddr;
        for(int c=0;c<3;c++)for(int h=0;h<640;h++)for(int w=0;w<640;w++){
            cv::Vec3b p=rsz.at<cv::Vec3b>(h,w); idata[c*640*640+h*640+w]=p[2-c];}
        hbSysFlushMem(&imem,HB_SYS_MEM_CACHE_CLEAN);

        hbDNNInferCtrlParam ip; HB_DNN_INITIALIZE_INFER_CTRL_PARAM(&ip);
        hbDNNTaskHandle_t th=nullptr; hbDNNTensor* op=&otensor;
        auto t0=std::chrono::high_resolution_clock::now();
        hbDNNInfer(&th,&op,&itensor,dnn,&ip);
        hbDNNWaitTaskDone(th,0); hbDNNReleaseTask(th);
        auto t1=std::chrono::high_resolution_clock::now();
        float ms=std::chrono::duration<float,std::milli>(t1-t0).count();

        hbSysFlushMem(&omem,HB_SYS_MEM_CACHE_INVALIDATE);
        float* odata=(float*)omem.virAddr;
        std::vector<DetectBox> boxes;
        for(int i=0;i<8400;i++){
            float mc=0;int mcl=0;
            for(int c=4;c<84;c++){float cf=sigmoid(odata[c*8400+i]);if(cf>mc){mc=cf;mcl=c-4;}}
            if(mc<0.55)continue;
            float cx=odata[0*8400+i],cy=odata[1*8400+i],w=odata[2*8400+i],h=odata[3*8400+i];
            DetectBox b;b.x1=cx-w/2;b.y1=cy-h/2;b.x2=cx+w/2;b.y2=cy+h/2;b.conf=mc;b.class_id=mcl;boxes.push_back(b);
        }
        std::vector<DetectBox> fb=nms(boxes,0.5);

        for(auto&b:fb){cv::rectangle(rsz,cv::Point((int)b.x1,(int)b.y1),cv::Point((int)b.x2,(int)b.y2),cv::Scalar(0,255,0),2);
            std::string lb=std::string(class_names[b.class_id])+":"+std::to_string(b.conf).substr(0,4);
            cv::putText(rsz,lb,cv::Point((int)b.x1,(int)b.y1-5),cv::FONT_HERSHEY_SIMPLEX,0.5,cv::Scalar(0,255,0),1);}
        cv::imwrite(std::string("/workspace/infer/results/result_")+files[idx],rsz);

        total_det+=fb.size(); total_time+=ms;
        if(fb.empty())empty_imgs.push_back(files[idx]);
        for(auto&b:fb)cls_cnt[class_names[b.class_id]]++;

        std::cout<<"["<<idx+1<<"/"<<files.size()<<"] "<<files[idx]<<" - "<<fb.size()<<"个目标 "<<ms<<"ms";
        if(!fb.empty()){std::cout<<" (";for(size_t k=0;k<fb.size();k++){if(k)std::cout<<", ";std::cout<<class_names[fb[k].class_id]<<":"<<std::to_string(fb[k].conf).substr(0,4);}std::cout<<")";}
        std::cout<<std::endl;
    }

    std::cout<<std::endl<<"========== 批量测试汇总报告 =========="<<std::endl;
    std::cout<<"测试图片: "<<files.size()<<"张 | 检测目标: "<<total_det<<"个 | 平均每张: "<<(float)total_det/files.size()<<"个"<<std::endl;
    std::cout<<"总推理时间: "<<total_time<<"ms | 平均延迟: "<<total_time/files.size()<<"ms/张（仿真）"<<std::endl;
    std::cout<<std::endl<<"--- 类别分布 ---"<<std::endl;
    for(auto&kv:cls_cnt)std::cout<<"  "<<kv.first<<": "<<kv.second<<"个"<<std::endl;
    std::cout<<std::endl<<"--- 无检测图片 ("<<empty_imgs.size()<<"张) ---"<<std::endl;
    for(auto&f:empty_imgs)std::cout<<"  "<<f<<std::endl;
    if(empty_imgs.empty())std::cout<<"  （无）"<<std::endl;
    std::cout<<std::endl<<"结果图保存: /workspace/infer/results/"<<std::endl;

    hbSysFreeMem(&imem);hbSysFreeMem(&omem);hbDNNRelease(packed);
    return 0;
}
