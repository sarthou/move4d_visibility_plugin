#include "VisibilityGrid/PointingPlanner.hpp"

#undef  QT_LIBRARY
#include <move4d/API/project.hpp>
//#include <move4d/API/Grids/MultiGrid.hpp>
#include <move4d/API/Grids/NDGrid.hpp>
#include <move4d/API/Grids/NDGridAlgo.hpp>
#include "VisibilityGrid/VisibilityGrid.hpp"
#include "VisibilityGrid/VisibilityGridLoader.hpp"
#include <libmove3d/util/proto/p3d_angle_proto.h>

#include <move4d/API/Device/objectrob.hpp>
#include <move4d/utils/Geometry.h>
#include <move4d/utils/mathfunc.hpp>
#include <move4d/planner/cost_space.hpp>
#include <move4d/API/Graphic/DrawablePool.hpp>
#include <move4d/API/Collision/collisionInterface.hpp>
#include <move4d/API/Collision/CylinderCollision.hpp>

#include <boost/bind.hpp>

#include <chrono>
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

using namespace std::chrono;

namespace move4d {



INIT_MOVE3D_STATIC_LOGGER(PlanningData,"move4d.visibilitygrid.pointingplanner.data");

struct CompareCellPtr
{
    bool operator()(PlanningData::Cell *const &a,PlanningData::Cell *const &b){
        return b<a;
    }
};

PlanningData::Cell PlanningData::run(bool read_parameters)
{
    M3D_DEBUG("PointingPlanner::run start");
    ENV.setBool(Env::isRunning,true);
    if(read_parameters) getParameters();
    this->resetFromCurrentInitPos();
    balls->balls_values.clear();
    VisibilityGrid3d *vis_grid=dynamic_cast<VisibilityGridLoader*>(ModuleRegister::getInstance()->module(VisibilityGridLoader::name()))->grid();
    CompareCellPtr comp;
    std::vector<Cell*> open_heap;
    VisibilityGrid3d::SpaceCoord vis_cell_size=vis_grid->getCellSize();
    //API::MultiGrid<float,vis_size[0],vis_size[1],vis_size[0],vis_size[1]> grid;
    Grid::SpaceCoord cell_size;
    cell_size[0]=cell_size[2]=vis_cell_size[0];
    cell_size[1]=cell_size[3]=vis_cell_size[1];
    std::vector<double> envSize(8,0.);
    envSize[0]=envSize[4]=global_Project->getActiveScene()->getBounds()[0]; //x min
    envSize[1]=envSize[5]=global_Project->getActiveScene()->getBounds()[1]; //x max
    envSize[2]=envSize[6]=global_Project->getActiveScene()->getBounds()[2]; //y min
    envSize[3]=envSize[7]=global_Project->getActiveScene()->getBounds()[3]; //y max

    bool adjust=false;
    Grid grid(cell_size,adjust,envSize);
    r=global_Project->getActiveScene()->getActiveRobot();
    assert(this->h);
    //h=global_Project->getActiveScene()->getRobotByNameContaining("HUMAN");

    Grid::ArrayCoord coord;
    Grid::SpaceCoord pos;
    Eigen::Vector2d from;
    for(uint i=0;i<2;++i){
        from[i]=pos[0+i]=r->getInitialPosition()->at(6+i);
        pos[2+i]=h->getInitialPosition()->at(6+i);
    }

    coord=grid.getCellCoord(pos);
    Cell *start=createCell(coord,grid.getCellCenter(coord));
    start->open=true;
    grid.getCell(coord)=start;
    open_heap.push_back(start);
    std::push_heap(open_heap.begin(),open_heap.end(),comp);

    Cell *best=start;
    uint count(0);
    uint iter_of_best{0};

    unsigned int neighbours_number = grid.neighboursNumber();
    unsigned int i=0;
    Cell *c;
    bool found_best = false;

    srand (time(NULL));
    high_resolution_clock::time_point t1 = high_resolution_clock::now();

    for(count=0;count<160000 && open_heap.size();++count){
        //std::pop_heap(open_heap.begin(),open_heap.end(),comp);
        coord = open_heap.back()->coord;
        open_heap.pop_back();

        for (i=0;i<neighbours_number;++i)
        {
            Grid::ArrayCoord neigh=grid.getNeighbour(coord,i);
            try
            {
                bool compute_cost=false;
                c=grid[neigh];
                if(!c)
                {
                    c=new Cell(neigh,grid.getCellCenter(neigh));
                    c->col=0.;
                    grid[neigh]=c;
                    c->cost.constraint(MyConstraints::COL)=std::numeric_limits<float>::infinity();
                    compute_cost=true;
                }

                if(!c->open)
                {
                  if(isTooFar(c,start))
                    c->open=true; //do not enter in the "if" bellow, hence ignores its neighbours

                  if(!c->open)
                  {
                    if(compute_cost)
                        computeCost(c);

                    if(c->col > best->col)
                        c->open=true; //skip also if in collision (and we were not)
                    else
                    {
                      if(!found_best)
                      {
                        open_heap.push_back(c);
                        //std::push_heap(open_heap.begin(),open_heap.end(),comp); 
                      }

                      c->open=true;
                      if(c->cost < best->cost)
                      {
                          //Cell::CostType xx=best->cost;
                          if(best->cost.toDouble() < 1000)
                            found_best = true;

                          best = c;
                          iter_of_best=count;
                          //setRobots(r,h,best);
                          std::cout << "best: " << best->cost.toDouble() << " : " << best->cost.cost(MyCosts::COST) << std::endl;
                          if(found_best)
                          {
                            open_heap.clear();
                            open_heap.push_back(c);
                          }
                      }
                    }
                  }
                }
            }
            catch (Grid::out_of_grid &e)
            {
                //that's normal, just keep on going.
            }
        }
    }
    //visibEngine->finish();
    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    duration<double> time_span = duration_cast<duration<double>>(t2 - t1);

    std::cout << "It took me " << time_span.count() << std::endl;

    setRobots(r,h,best);
    M3D_DEBUG("done "<<best->cost.toDouble()<<" found at iteration #"<<iter_of_best
              <<"\nit: "<<count<<" / "<<grid.getNumberOfCells()
              <<"\ntarget: "<<targets[best->target]->getName()
              <<"\n\tCcol="<<best->cost.constraint(MyConstraints::COL)
              <<"\n\tCvis="<<best->cost.constraint(MyConstraints::VIS)
              <<"\n\tcost="<<best->cost.cost(MyCosts::COST)
              <<"\n\ttime="<<best->cost.cost(MyCosts::TIME)
              );
    {
    std::vector<std::string> visible_landmarks;
    API::Parameter::lock_t lock;
    if(targets.size()>1){
        //check other visible targets
        auto visib=getVisibilites(h,best->vPosHuman());
        API::Parameter &otherVisParam = API::Parameter::root(lock)["PointingPlanner"]["result"]["other_visible"];
        otherVisParam=API::Parameter(API::Parameter::ArrayValue);
        for(uint i=0;i<visib.size();++i){
            if(i!=best->target && visib[i]<vis_threshold){
                visible_landmarks.push_back(targets[i]->getName());
                otherVisParam.append(targets[i]->getName());
                M3D_DEBUG("other visible landmark: "<< targets[i]->getName());
            }
        }
    }
    API::Parameter &targetParam = API::Parameter::root(lock)["PointingPlanner"]["result"]["target"];
    targetParam=API::Parameter(API::Parameter::ArrayValue);
    targetParam.append(targets[best->target]->getName());
    }

    Cell best_copy=*best;
    for(uint i=0;i<grid.getNumberOfCells();++i){
        if(grid.getCell(i)) delete grid.getCell(i);
    }\
    ENV.setBool(Env::isRunning,false);
    M3D_DEBUG("PointingPlanner::run end");
    if(!best_copy.cost.isValid()){
        M3D_INFO("PointingPlanner::run invalid solution");
        throw best_copy.cost.toDouble();
    }

    return best_copy;
}

PlanningData::Cell *PlanningData::createCell(Grid::ArrayCoord coord, Grid::SpaceCoord pos){
    Cell *cell=new Cell(coord,pos);
    cell->col = 0;
    computeCost(cell);
    return cell;
}

void PlanningData::setRobots(Robot *a, Robot *b, Cell *cell){
    RobotState qa,qb;
    qa=*a->getCurrentPos();
    qb=*b->getCurrentPos();
    for(uint i=0;i<2;++i){
        qa[6+i]=cell->getPos(0)[i];
        qb[6+i]=cell->getPos(1)[i];
    }
    qa.setCost(cell->cost.toDouble());
    qb.setCost(cell->cost.toDouble());
    a->setAndUpdate(qa);
    b->setAndUpdate(qb);
    moveHumanToFaceTarget(cell,cell->target);
    moveRobotToHalfAngle(cell,cell->target);
}

PlanningData::Cost PlanningData::targetCost(Cell *c, uint i, float visib,float visib_r)
{
    visib = std::max(visib,visib_r);
    visib = std::max(0.f,visib);
    Cost cost;
    float kh(1-mh), kr(1-mr);
    float c_visib = bounded_affine<float>(visib,0.2f,vis_threshold,0.f,1.f);
    c_visib*=c_visib;
    Eigen::Vector3d rt,ht,hr;
    Eigen::Vector2d rt2,ht2,hr2;
    Robot *target=targets[i];
    c->target = i; //used by setRobots
    this->setRobots(r,h,c);
    rt =  target->getJoint(0)->getVectorPos() - r->getHriAgent()->perspective->getVectorPos();
    ht =  target->getJoint(0)->getVectorPos() - h->getHriAgent()->perspective->getVectorPos();
    hr = r->getHriAgent()->perspective->getVectorPos() - h->getHriAgent()->perspective->getVectorPos();
    for(uint i=0;i<2;++i){
        rt2[i]=rt[i];
        ht2[i]=ht[i];
        hr2[i]=hr[i];
    }
    rt2.normalize();
    ht2.normalize();
    hr2.normalize();

    float angle_h = std::acos(hr2.dot(ht2));//angle for the human to look at the target and the robot
    float angle_r = std::acos((-hr2).dot(rt2));//idem for robot -> human
    //visib = std::max(0.f,(1-visibility(i,ph))); //visibility cost (i.e. 1=worst, 0=best) of the target for the human
    float angle_persp = std::acos((ht2).dot(rt2)); // perspective difference weighted by the visibility of the target

    float c_angle_r,c_angle_h,c_angle_persp, c_route_dir;
    c_angle_h = bounded_affine<float>(angle_h,0.,float(M_PI));
    c_angle_r = bounded_affine<float>(angle_r,0,float(M_PI));
    c_angle_persp = bounded_affine<float>(angle_persp,0,float(M_PI)); //perspective difference

    c_route_dir=getRouteDirTime(c,i);

    cost.constraint(MyConstraints::COL)=0.f;
    cost.constraint(MyConstraints::VIS) = std::max<float>(0.,visib - vis_threshold);
    cost.constraint(MyConstraints::ANGLE) = std::max<float>(0., std::pow(angle_h - desired_angle_h,2.) - desired_angle_h_tolerance*desired_angle_h_tolerance);
    cost.cost(MyCosts::COST) = c_angle_r * kr + c_angle_h * kh + c_angle_persp * ka;
    cost.cost(MyCosts::TIME) = c_route_dir;
    cost.cost(MyCosts::VISIB) = c_visib;

    std::map<std::string,double> costDetails = global_costSpace->getCostDetails();
    costDetails[target->getName()+" angle h"]=     double(180./M_PI * angle_h);
    costDetails[target->getName()+" angle r"]=     double(180./M_PI * angle_r);
    costDetails[target->getName()+" angle persp"]= double(180./M_PI * angle_persp);
    costDetails[target->getName()+" visib"]=       double(visib);
    costDetails[target->getName()+" cost"]=       double(cost.cost(MyCosts::COST));
    global_costSpace->setCostDetails(std::move(costDetails));

    return cost;

}

float PlanningData::getRouteDirTime(PlanningData::Cell *, uint i)
{
    if(routeDirTimes.size()==0){return 0.f;}
    return routeDirTimes.at(i);
}

PlanningData::Cost PlanningData::computeCost(Cell *c)
{
    global_costSpace->setCostDetails(std::map<std::string,double>{});
    c->cost=Cell::CostType{};
    float kh(1-mh), kr(1-mr);
    float cost;
    std::vector<float> angle_h(targets.size());
    std::vector<float> angle_r(targets.size());
    std::vector<float> angle_persp(targets.size());
    std::vector<float> visib(targets.size());
    std::vector<float> visib_rob(targets.size());

    Eigen::Vector3d hr = r->getHriAgent()->perspective->getVectorPos() - h->getHriAgent()->perspective->getVectorPos();
    if(hr.squaredNorm()){
        //if vector is non null
        hr.normalize();
    }
    Eigen::Vector2d pr,ph;
    pr=c->vPosRobot();
    ph=c->vPosHuman();
    //move agents to positions of the cell: -> done in targetCost
    //m3dGeometry::setBasePosition2D(r,pr);
    //m3dGeometry::setBasePosition2D(h,ph);

    //for each target get its related values
    visib = getVisibilites(h,c->vPosHuman());
    visib_rob = getVisibilites(r,c->vPosRobot());
    Cost best_target_cost;
    Cost worst_target_cost;
    Cost worst_optional_cost;
    uint best_target{-1u}, worst_target{-1u}, worst_optional{-1u};
    best_target_cost.constraint(MyConstraints::COL)=std::numeric_limits<float>::infinity();
    worst_target_cost.constraint(MyConstraints::COL)=-std::numeric_limits<float>::infinity();
    worst_optional_cost.constraint(MyConstraints::COL)=-std::numeric_limits<float>::infinity();

    for (uint i=0;i<indexFirstOptionalTarget;++i)
    {
        Cost t = targetCost(c,i,visib[i],visib_rob[i]);
        if(t<best_target_cost){
            best_target_cost=t;
            best_target=i;
        }
        if(worst_target_cost<t){
            worst_target_cost=t;
            worst_target=i;
        }
        if(worst_optional_cost<t){
            worst_optional_cost=t;
            worst_optional=i;
        }
    }

    for (uint i=indexFirstOptionalTarget;i<targets.size();++i){
        Cost t = targetCost(c,i,visib[i],visib_rob[i]);
        if(worst_optional_cost<t){
            worst_optional_cost=t;
            worst_optional=i;
        }
    }
    c->target = best_target;
    float c_dist_r,c_dist_h,c_prox,c_time,c_time_robot;
    int col;
    {
        float dist_r,dist_h,dist_target{0};
        float time_ask_to_move{0};
        std::array<float,2> ah,ar;
        ar=c->posRobot();
        ah=c->posHuman();
        dist_r=distGrid_r.getCostPos(ar);
        dist_h=distGrid_h.getCostPos(ah);
        if(dist_h >= ask_to_move_dist_trigger)
            time_ask_to_move += ask_to_move_duration;

        if(usePhysicalTarget)
            dist_target=distGrid_physicalTarget.getCostPos(ah);

        c_dist_r = std::pow(dist_r+1,kr*kd+1)-1.f;//dist robot
        c_dist_h = std::pow(dist_h+1,kh*kd+1)-1.f;//dist human
        float time_guiding=std::max(dist_h/sh,dist_r/sr);
        c_time = time_guiding + dist_target/sh + time_ask_to_move;
        c_time_robot=time_guiding + dist_r/sr + time_ask_to_move;

        API::CylinderCollision cylinderCol(global_Project->getCollision());
        col = 3;
        col -= int(freespace_h.getCell(ah));
        col -= int(freespace_r.getCell(ar));
        col -= int(cylinderCol.moveCheck(r,Eigen::Vector3d(pr[0],pr[1],0.),h,Eigen::Vector3d(ph[0],ph[1],0.)));
    }
    c_prox = std::abs(dp-float((pr-ph).norm())); //proxemics

    cost = (1.f + ktr*c_time_robot + kt*c_time + (ktr+kt)*best_target_cost.cost(MyCosts::TIME) + kv*worst_optional_cost.cost(MyCosts::VISIB))
            *
            (std::pow(worst_optional_cost.cost(MyCosts::COST),2.f)+std::pow(worst_target_cost.cost(MyCosts::COST),2.f)) ;
    if(cost!=cost || cost>=std::numeric_limits<float>::max()){//nan or inf
        cost=std::numeric_limits<float>::infinity();
    }
    float time = std::max(c_time_robot,c_time) + best_target_cost.cost(MyCosts::TIME);

    /*std::map<std::string,double> costDetails = global_costSpace->getCostDetails();
    //costDetails["angle h"]=     double(c_angle_h);
    //costDetails["angle r"]=     double(c_angle_r);
    //costDetails["angle persp"]= double(c_angle_persp);
    costDetails["0dist r"]=      double(c_dist_r);
    costDetails["0dist h"]=      double(c_dist_h);
    costDetails["1time human"]=  double(c_time);
    costDetails["1time robot"]=  double(c_time_robot);
    costDetails["2proxemics"]=   double(c_prox);
    costDetails["2time dir"] =   double(best_target_cost.cost(MyCosts::TIME));
    //costDetails["visib"]=       double(c_visib);
    costDetails["2target cost"]= double(worst_optional_cost.cost(MyCosts::COST));
    global_costSpace->setCostDetails(std::move(costDetails));*/

    c->cost.cost(MyCosts::COST)=cost;
    c->cost.cost(MyCosts::TIME)=0.f;

    c->col = (col!=0);
    c->cost.constraint(MyConstraints::COL) = col;
    c->cost.constraint(MyConstraints::VIS) = worst_target_cost.constraint(MyConstraints::VIS);
    c->cost.constraint(MyConstraints::DIST) = std::max(0.f,c_prox*c_prox - prox_tol*prox_tol);
    c->cost.constraint(MyConstraints::RTIME) = std::max(0.f, c_time_robot+best_target_cost.cost(MyCosts::TIME) - max_time_r);
    c->cost.constraint(MyConstraints::ANGLE) = worst_target_cost.constraint(MyConstraints::ANGLE);
    c->vis = c->cost.constraint(MyConstraints::VIS) <=0.f; // if visib is better than ..

    return c->cost;
}

std::vector<float> PlanningData::getVisibilites(Robot *r, const Eigen::Vector2d &pos2d)
{
    std::vector<float> visib;
    Eigen::Affine3d jnt_pos = r->getHriAgent()->perspective->getMatrixPos();
    jnt_pos.translationExt()[0]=pos2d[0];
    jnt_pos.translationExt()[1]=pos2d[1];
    Eigen::Vector3f pos=jnt_pos.translation().cast<float>();
    VisibilityGrid3d::SpaceCoord pgrid{{pos[0],pos[1],pos[2]}};
    try{
        VisibilityGrid3d::reference cell=visibilityGrid->getCell(pgrid);
        VisibilityGrid3d::SpaceCoord c =visibilityGrid->getCellCenter(visibilityGrid->getCellCoord(pgrid));
        M3D_TRACE("cell "<<c[0]<<" "<<c[1]<<" "<<c[2]);
        for(uint i=0;i<targets.size();++i){
            M3D_TRACE("\t"<<targets[i]->getName()<<" "<<cell[targets[i]]);
            visib.push_back(1.f-cell[targets[i]]);
        }
    }catch(VisibilityGrid3d::out_of_grid &){
        visib.assign(targets.size(),1.f);
    }
    //ogre_rob->getRobotNode()->setVisible(false);
    //visibEngine->computeVisibilityFrom(jnt_pos);
    //for ( Robot *t : targets){
    //    visib.push_back(1.f-1.f/0.007f * float(visibEngine->getVisibilityInFovPercent(t)));
    //}
    //ogre_rob->getRobotNode()->setVisible(true);
    return visib;
}

float PlanningData::computeStateCost(RobotState &q)
{
    reinit();
    if(q.getRobot() == r){
        Grid::ArrayCoord coord{{0,0,0,0}};
        Grid::SpaceCoord pos;
        for(unsigned int i=0;i<2;i++){
            pos[i]=q.at(6+i);
            pos[i+2]=h->getCurrentPos()->at(6+i);
        }
        Cell c(coord,pos);
        try{
            computeCost(&c);
            setRobots(r,h,&c);
        }catch(std::out_of_range &e){
            M3D_DEBUG("out_of_range in PointingPlanner::PlanningData::computeStateCost "<<e.what());
            return 0.;
        }
        return c.cost.toDouble();
    }else if(q.getRobot() == h){
        Grid::ArrayCoord coord={0,0,0,0};
        Grid::SpaceCoord pos;
        for(unsigned int i=0;i<2;i++){
            pos[i]=r->getCurrentPos()->at(6+i);
            pos[i+2]=q.at(6+i);
        }
        Cell c(coord,pos);
        try{
            computeCost(&c);
            setRobots(r,h,&c);
        }catch(std::out_of_range &e){
            M3D_DEBUG("out_of_range in PointingPlanner::PlanningData::computeStateCost "<<e.what());
            return 0.;
        }
        return c.cost.toDouble();
    }else{
        Grid::ArrayCoord coord={0,0,0,0};
        Grid::SpaceCoord pos;
        for(unsigned int i=0;i<2;i++){
            pos[i]=r->getCurrentPos()->at(6+i);
            pos[i+2]=h->getCurrentPos()->at(6+i);
        }
        Cell c(coord,pos);
        try{
            computeCost(&c);
            setRobots(r,h,&c);
        }catch(std::out_of_range &e){
            M3D_DEBUG("out_of_range in PointingPlanner::PlanningData::computeStateCost "<<e.what());
            return 0.;
        }
        return c.cost.toDouble();
    }
    return 0.f;
}
float PlanningData::computeStateVisiblity(RobotState &state){
    getParameters();
    Robot *r=state.getRobot();
    Eigen::Vector2d pos{state[6],state[7]};
    std::vector<float> vis = getVisibilites(r,pos);
    if(vis.size()){
        M3D_INFO("visibility computed for "<<targets[0]->getName());
        return vis[0];
    }else if(vis.empty()){
        M3D_ERROR("No target set");
        return 0.;
    }
}

void PlanningData::moveHumanToFaceTarget(Cell *c, uint target_id)
{
    Eigen::Vector2d pt = m3dGeometry::getConfBase2DPos(*targets[target_id]->getCurrentPos());
    Eigen::Vector2d ph = c->vPosHuman();
    float angle=m3dGeometry::angle(pt-ph);
    RobotState q=*h->getCurrentPos();
    q[9]=q[10]=0.;
    q[11]=angle;
    h->setAndUpdate(q);
}

void PlanningData::moveRobotToHalfAngle(Cell *c, uint target_id)
{
    Eigen::Vector2d pt = m3dGeometry::getConfBase2DPos(*targets[target_id]->getCurrentPos());
    Eigen::Vector2d ph(c->vPosHuman());
    Eigen::Vector2d pr(c->vPosRobot());
    using namespace m3dGeometry;
    float a= angle(ph-pr) + angle(pt-pr,ph-pr)/2;
    RobotState q=*r->getCurrentPos();
    q[9]=q[10]=0.;//enforce orientation (fix due to non-zero orientation in the original position when integrated with ros)
    q[11]=a;
    r->setAndUpdate(q);
}

float PlanningData::element(const std::vector<float> &values, float factor){
    assert(values.size());
    if(values.empty()){return 1.f;}
    return *std::max_element(values.begin(),values.end()) * factor;
}

float PlanningData::visibility(uint target_i, const Eigen::Vector3d &pos){
    VisibilityGrid3d::SpaceCoord p{pos[0],pos[1],pos[2]};
    try{
        VisibilityGrid3d::reference cell=visibilityGrid->getCell(p);
        return cell[targets[target_i]];
    }catch(VisibilityGrid3d::out_of_grid &){
        return 0.f;
    }
}

PlanningData::PlanningData(Robot *r, Robot *h):
    r(r),h(h)
{
    visibilityGrid=dynamic_cast<VisibilityGridLoader*>(ModuleRegister::getInstance()->module(VisibilityGridLoader::name()))->grid();
    cyl_r = r->getObjectRob()->getCylinder();
    cyl_h = h->getObjectRob()->getCylinder();

    //visibEngine = new MoveOgre::VisibilityEngine(Ogre::Degree(360.f),Ogre::Degree(90.f),64u);

    reinit();

}

PlanningData::~PlanningData()
{
    //delete visibEngine;
}

void PlanningData::getParameters()
{
    API::Parameter::lock_t lock;
    mr=API::Parameter::root(lock)["PointingPlanner"]["mobrob"].asDouble();
    mh=API::Parameter::root(lock)["PointingPlanner"]["mobhum"].asDouble();
    sh=API::Parameter::root(lock)["PointingPlanner"]["speedhum"].asDouble();
    sr=API::Parameter::root(lock)["PointingPlanner"]["speedrob"].asDouble();
    kt=API::Parameter::root(lock)["PointingPlanner"]["ktime"].asDouble();
    ktr=API::Parameter::root(lock)["PointingPlanner"]["ktimerob"].asDouble();
    ka=API::Parameter::root(lock)["PointingPlanner"]["kangle"].asDouble();
    kp=API::Parameter::root(lock)["PointingPlanner"]["kproxemics"].asDouble();
    dp=API::Parameter::root(lock)["PointingPlanner"]["distproxemics"].asDouble();
    prox_tol=API::Parameter::root(lock)["PointingPlanner"]["proxemics_tolerance"].asDouble();
    kd=API::Parameter::root(lock)["PointingPlanner"]["kdist"].asDouble();
    kv=API::Parameter::root(lock)["PointingPlanner"]["kvisib"].asDouble();
    max_dist=API::Parameter::root(lock)["PointingPlanner"]["maxdist"].asDouble();
    max_time_r=API::Parameter::root(lock)["PointingPlanner"]["maxtime"].asDouble();
    vis_threshold=API::Parameter::root(lock)["PointingPlanner"]["vis_threshold"].asDouble();
    desired_angle_h=API::Parameter::root(lock)["PointingPlanner"]["desired_angle_h"].asDouble();
    desired_angle_h_tolerance=API::Parameter::root(lock)["PointingPlanner"]["desired_angle_h_tolerance"].asDouble();
    usePhysicalTarget=API::Parameter::root(lock)["PointingPlanner"]["use_physical_target"].asBool();
    if ( API::Parameter::root(lock)["PointingPlanner"]["physical_target_pos"].type() == API::Parameter::ArrayValue){
        physicalTarget[0]=API::Parameter::root(lock)["PointingPlanner"]["physical_target_pos"][0].asDouble();
        physicalTarget[1]=API::Parameter::root(lock)["PointingPlanner"]["physical_target_pos"][1].asDouble();
    }else{
        usePhysicalTarget=false;
    }
    ask_to_move_dist_trigger = API::Parameter::root(lock)["PointingPlanner"]["ask_to_move_dist_trigger"].asDouble();
    ask_to_move_duration = API::Parameter::root(lock)["PointingPlanner"]["ask_to_move_duration"].asDouble();

    if(API::Parameter::root(lock)["PointingPlanner"].hasKey("human")){
        std::string human_name = API::Parameter::root(lock)["PointingPlanner"]["human"].asString();
        h = global_Project->getActiveScene()->getRobotByName(human_name);
        if(!h){
            M3D_ERROR("human not found ("<<human_name<<")");
        }
    }else{
        M3D_ERROR("no human set");
    }


    API::Parameter &ptargets = API::Parameter::root(lock)["PointingPlanner"]["targets"];
    targets.clear();
    for(uint i=0;i<ptargets.size();++i){
        targets.push_back(global_Project->getActiveScene()->getRobotByName(ptargets[i].asString()));
        if(!targets.back()){
             M3D_ERROR("no object with name "<<ptargets[i].asString()<<" known to be set as a pointing target");
        }
    }
    indexFirstOptionalTarget = targets.size();
    API::Parameter &poptTargets = API::Parameter::root(lock)["PointingPlanner"]["optional_targets"];
    for(uint i=0;i<poptTargets.size();++i){
        targets.push_back(global_Project->getActiveScene()->getRobotByName(poptTargets[i].asString()));
        if(!targets.back()){
             M3D_ERROR("no object with name "<<poptTargets[i].asString()<<" known to be set as a pointing target");
        }
    }
}

void PlanningData::resetFromCurrentInitPos()
{
    start_r = *r->getInitialPosition();
    start_h = *h->getInitialPosition();

    start_p_r = m3dGeometry::getConfBase2DPos(start_r);
    start_p_h = m3dGeometry::getConfBase2DPos(start_h);

    balls=std::shared_ptr<Graphic::LinkedBalls2d>(new Graphic::LinkedBalls2d);
    balls->name="PointingPlanner";

    initCollisionGrids();
    API::ndGridAlgo::Dijkstra<API::nDimGrid<bool,2>,float>::SpaceCoord fromr,fromh,phyTargetPos;
    fromr[0]=start_p_r[0];
    fromr[1]=start_p_r[1];
    fromh[0]=start_p_h[0];
    fromh[1]=start_p_h[1];
    phyTargetPos[0]=physicalTarget[0];
    phyTargetPos[1]=physicalTarget[1];
    try{
        distGrid_r = API::ndGridAlgo::Dijkstra<API::nDimGrid<bool,2>,float>(&freespace_r,freespace_r.getCellCoord(fromr));
    }catch(Grid::out_of_grid &e){
        M3D_INFO("cannot compute the dijkstra for the robot, from "<<fromr[0]<<","<<fromr[1]);
        throw e;
    }
    try{
        distGrid_h = API::ndGridAlgo::Dijkstra<API::nDimGrid<bool,2>,float>(&freespace_h,freespace_h.getCellCoord(fromh));
    }catch(Grid::out_of_grid &e){
        M3D_INFO("cannot compute the dijkstra for the human, from "<<fromh[0]<<","<<fromh[1]);
        throw e;
    }
    if(usePhysicalTarget)
    try{
        distGrid_physicalTarget= API::ndGridAlgo::Dijkstra<API::nDimGrid<bool,2>,float>(&freespace_h,freespace_h.getCellCoord(phyTargetPos));
    }catch(Grid::out_of_grid &e){
        M3D_INFO("cannot compute the dijkstra to the target, from "<<phyTargetPos[0]<<","<<phyTargetPos[1]);
        throw e;
    }
    Graphic::DrawablePool::sAddGrid2Dfloat(std::shared_ptr<Graphic::Grid2Dfloat>(new Graphic::Grid2Dfloat{"distance human",API::nDimGrid<float,2>(distGrid_h.getGrid()),true}));
    Graphic::DrawablePool::sAddGrid2Dfloat(std::shared_ptr<Graphic::Grid2Dfloat>(new Graphic::Grid2Dfloat{"distance robot",API::nDimGrid<float,2>(distGrid_r.getGrid()),true}));
    if(usePhysicalTarget)
        Graphic::DrawablePool::sAddGrid2Dfloat(std::shared_ptr<Graphic::Grid2Dfloat>(new Graphic::Grid2Dfloat{"distance target",API::nDimGrid<float,2>(distGrid_physicalTarget.getGrid()),true}));
}

void PlanningData::reinit(){
    getParameters();
    try{
    resetFromCurrentInitPos();
    }catch (Grid::out_of_grid &e){
        M3D_INFO("out of grid in PlanningData::reinit, won't work for now");
    }
}

void PlanningData::initCollisionGrids()
{
    VisibilityGrid3d::SpaceCoord vis_cell_size=visibilityGrid->getCellSize();
    API::nDimGrid<bool,2>::SpaceCoord cell_size;
    cell_size[0]=vis_cell_size[0];
    cell_size[1]=vis_cell_size[1];
    std::vector<double> envSize(4,0.);
    envSize[0]=global_Project->getActiveScene()->getBounds()[0]; //x min
    envSize[1]=global_Project->getActiveScene()->getBounds()[1]; //x max
    envSize[2]=global_Project->getActiveScene()->getBounds()[2]; //y min
    envSize[3]=global_Project->getActiveScene()->getBounds()[3]; //y max
    bool adjust=false;
    freespace_h=API::nDimGrid<bool,2>(cell_size,adjust,envSize);
    freespace_r=API::nDimGrid<bool,2>(cell_size,adjust,envSize);
    initCollisionGrid(freespace_h,h);
    initCollisionGrid(freespace_r,r);

    Graphic::DrawablePool::sAddGrid2Dfloat(std::shared_ptr<Graphic::Grid2Dfloat>(new Graphic::Grid2Dfloat{"collision human",API::nDimGrid<float,2>(freespace_h)}));
    Graphic::DrawablePool::sAddGrid2Dfloat(std::shared_ptr<Graphic::Grid2Dfloat>(new Graphic::Grid2Dfloat{"collision robot",API::nDimGrid<float,2>(freespace_r)}));
}

void PlanningData::initCollisionGrid(API::nDimGrid<bool,2> &grid, Robot *a)
{
    API::CollisionInterface *coll=global_Project->getCollision();
    assert(coll);
    API::CylinderCollision cylinderColl(coll);
    for(uint i=0;i<grid.getNumberOfCells();++i){
        API::nDimGrid<bool,2>::SpaceCoord c = grid.getCellCenter(grid.getCellCoord(i));
        Eigen::Vector3d p{c[0],c[1],0.};
        //m3dGeometry::setBasePosition2D(a,p);
        grid[i]=cylinderColl.moveCheck(a,p,API::CollisionInterface::CollisionChecks(API::CollisionInterface::COL_ENV | API::CollisionInterface::COL_OBJECTS));
    }

}

} // namespace move4d
