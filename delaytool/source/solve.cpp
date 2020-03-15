#include <iostream>
#include <vector>
#include <cstdlib>
#include <map>
#include "solve.h"
#include "algo.h"
#include "tinyxml2/tinyxml2.h"

// На случай если chr == int (чтобы менять меньше кода в случае смены типа chr).
int atochr(const char* str) {
    return std::atoi(str);
}

// Решение задач жадным алгоритмом из XML-файла file_in и запись решений (расписаний) в XML-файл file_out
// Возвращает успешность выполнения, в случае неуспеха в cerr напечатается сообщение об ошибке.
bool solve(const std::string& file_in, const std::string& file_out) {
    tinyxml2::XMLDocument doc_in;
    auto err = doc_in.LoadFile(file_in.c_str());
    if(err) {
        std::cerr << "error opening input file: " << tinyxml2::XMLDocument::ErrorIDToName(err) << std::endl;
        return false;
    }
    auto tasks = doc_in.FirstChildElement("tasks");
    tinyxml2::XMLDocument doc_out;
    auto tasks_out = doc_out.NewElement("tasks");
    doc_out.InsertFirstChild(tasks_out);
    FILE *fp_out = fopen(file_out.c_str(), "w");
    if(fp_out == nullptr) {
        std::cerr << "error: cannot open " << file_out << std::endl;
        return false;
    }
    for(auto task = tasks->FirstChildElement("task");
        task != nullptr;
        task = task->NextSiblingElement("task"))
    {
        auto task_out = task->ShallowClone(&doc_out)->ToElement();
        tasks_out->InsertEndChild(task_out);
        std::vector<Job> jobs;
        // Чтобы скопировать всю информацию о работах из входного файла в выходной.
        std::map<int, tinyxml2::XMLElement*> jobs_xml;
        for(tinyxml2::XMLElement* job = task->FirstChildElement("job");
            job != nullptr;
            job = job->NextSiblingElement("job"))
        {
            chr id = atoi(job->Attribute("id"));
            chr len = atochr(job->Attribute("len"));
            chr dir1 = atochr(job->Attribute("dir1"));
            chr dir2 = atochr(job->Attribute("dir2"));
            jobs.emplace_back(id, len, dir1, dir2);
            jobs_xml[id] = job;
        }
        // Строим расписание алгоритмом.
        std::vector<JobInst> schedule = GreedySchedule(jobs);
        int result = schedule.size();
        task_out->SetAttribute("result", result);
        // Записываем расписание в xml.
        for(const auto& j: schedule) {
            // Копируем все атрибуты работы с данным id.
            auto job_out = jobs_xml[j.id]->DeepClone(&doc_out)->ToElement();
            task_out->InsertEndChild(job_out);
            // Собственно данные, полученные после решения задачи,
            // т.е. данные о положении экземпляра работы в расписании.
            job_out->SetAttribute("exec1", j.exec1);
            job_out->SetAttribute("exec2", j.exec2);
        }
    }
    err = doc_out.SaveFile(fp_out, false);
    fclose(fp_out);
    if(err) {
        std::cerr << "error writing to output file: " << tinyxml2::XMLDocument::ErrorIDToName(err) << std::endl;
        return false;
    }
    return true;
}