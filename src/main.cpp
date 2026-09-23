
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include "simulation.hpp"

#include <iostream>
#include <map>
#include <vector>
#include <filesystem>

int main()
{
    Simulation sim{1001, 150000};

    sim.add_instrument(1, "ALPHA", 100);
    sim.add_instrument(2,"BETA",100);
    sim.add_instrument(3,"GAMMA",100);
    sim.add_instrument(4,"DELTA",100);

    sim.set_recurring_costs(100,100);
    sim.introduce_holdings(1, 200);
    sim.introduce_holdings(2, 200);
    sim.introduce_holdings(3, 200);
    sim.introduce_holdings(4, 200);
    sim.set_bank_recycling(5000,1,0.25);
    for (int i = 1; i <= 30; ++i)
    {
        sim.queue_trader(TraderType::mean_value);
        sim.queue_trader(TraderType::random);
        sim.queue_trader(TraderType::portfolio_rebalancer);
    }

    float ticks_per_second = 20.0;
    bool running = true;

    std::vector<double> tick_history;
    std::map<int, std::vector<double>> price_history;

    SimulationSnapshot latest_snapshot = sim.get_snapshot();

    auto record_snapshot = [&]()
    {
        latest_snapshot = sim.get_snapshot();

        tick_history.push_back(
            static_cast<double>(latest_snapshot.tick)
        );

        for (int id : latest_snapshot.instrument_ids)
        {
            price_history[id].push_back(
                latest_snapshot.instrument_reference_price.at(id)
            );
        }
    };

    record_snapshot();

    if (!glfwInit())
    {
        std::cerr << "Failed to initialise GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(
        1280,
        720,
        "Market Simulation",
        nullptr,
        nullptr
    );

    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);

    glfwSwapInterval(1);

    const char* glsl_version = "#version 130";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    std::cout << "Working directory: "
            << std::filesystem::current_path()
            << '\n';

    std::cout << "ImGui ini: "
            << ImGui::GetIO().IniFilename
            << '\n';

    std::cout << "GL vendor: "
          << glGetString(GL_VENDOR) << '\n';

    std::cout << "GL renderer: "
            << glGetString(GL_RENDERER) << '\n';

    std::cout << "GL version: "
            << glGetString(GL_VERSION) << '\n';

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    double last_tick_time = glfwGetTime();

    while (!glfwWindowShouldClose(window))
    {
        double current_time = glfwGetTime();
        double tick_interval = 1.0 / ticks_per_second;

        int ticks_this_frame = 0;
        constexpr int max_ticks_per_frame = 20;

        while (running &&
            current_time - last_tick_time >= tick_interval &&
            ticks_this_frame < max_ticks_per_frame)
        {
            sim.tick();
            record_snapshot();

            last_tick_time += tick_interval;
            ++ticks_this_frame;
        }
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(
            0,
            ImGui::GetMainViewport()
        );

        ImGui::Begin("Market Simulation");
        ImGui::Text("Tick: %d", latest_snapshot.tick);

        double window_width = 600.0;

        double x_max = static_cast<double>(latest_snapshot.tick);
        double x_min = std::max(0.0, x_max - window_width);

        double y_min = std::numeric_limits<double>::max();
        double y_max = std::numeric_limits<double>::lowest();

        for (int id : latest_snapshot.instrument_ids)
        {
            const auto& prices = price_history.at(id);

            for (std::size_t i = 0; i < tick_history.size(); ++i)
            {
                if (tick_history[i] < x_min)
                {
                    continue;
                }

                y_min = std::min(y_min, prices[i]);
                y_max = std::max(y_max, prices[i]);
            }
        }
        double range = y_max - y_min;

        if (range < 1.0)
        {
            range = 1.0;
        }

        double padding = range * 0.10;
        ImVec2 plot_size = ImGui::GetContentRegionAvail();
        if (ImPlot::BeginPlot("Reference Prices", plot_size))
        {
            ImPlot::SetupAxes("Tick", "Reference Price");

            ImPlot::SetupAxisLimits(
                ImAxis_X1,
                x_min,
                x_max,
                ImGuiCond_Always
            );

            ImPlot::SetupAxisLimits(
                ImAxis_Y1,
                std::max(0.0, y_min - padding),
                y_max + padding,
                ImGuiCond_Always
            );

            for (int id : latest_snapshot.instrument_ids)
            {
                const auto& name = latest_snapshot.instrument_names.at(id);
                const auto& prices = price_history.at(id);

                if (!tick_history.empty() && !prices.empty())
                {
                    ImPlot::PlotLine(
                        name.c_str(),
                        tick_history.data(),
                        prices.data(),
                        static_cast<int>(prices.size())
                    );
                }
            }

            ImPlot::EndPlot();
        }

        ImGui::End();

        ImGui::Begin("Population");

        ImGui::Text("Active traders: %d",
                    latest_snapshot.active_total_traders);

        ImGui::Text(
            "Random: %d | Cash: %lld | Wealth: %lld | Cash %%: %.1f",
            latest_snapshot.random,
            static_cast<long long>(latest_snapshot.random_cash),
            static_cast<long long>(latest_snapshot.random_portfolio_value),
            latest_snapshot.random_cash_fraction
        );

        ImGui::Text(
            "Mean value: %d | Cash: %lld | Wealth: %lld | Cash %%: %.1f",
            latest_snapshot.mean_reversion,
            static_cast<long long>(latest_snapshot.mean_reversion_cash),
            static_cast<long long>(latest_snapshot.mean_reversion_portfolio_value),
            latest_snapshot.mean_reversion_cash_fraction
        );

        ImGui::Text(
            "Rebalancer: %d | Cash: %lld | Wealth: %lld | Cash %%: %.1f",
            latest_snapshot.portfolio_rebalancer,
            static_cast<long long>(latest_snapshot.portfolio_rebalancer_cash),
            static_cast<long long>(latest_snapshot.portfolio_rebalancer_portfolio_value),
            latest_snapshot.portfolio_rebalancer_cash_fraction
        );


        ImGui::End();

        ImGui::Begin("Bank");

        ImGui::Text("Cash: %lld",
                    static_cast<long long>(latest_snapshot.bank_cash));

        ImGui::Text("Cash redistrbuted this tick: %lld",
                    static_cast<long long>(latest_snapshot.bank_redistributed_this_tick));

        for (int id : latest_snapshot.instrument_ids)
        {
            const auto& name =
                latest_snapshot.instrument_names.at(id);

            int holdings = 0;
            int reserved = 0;

            if (latest_snapshot.bank_holdings.contains(id))
            {
                holdings = latest_snapshot.bank_holdings.at(id);
            }

            if (latest_snapshot.bank_reserved_holdings.contains(id))
            {
                reserved = latest_snapshot.bank_reserved_holdings.at(id);
            }

            ImGui::Text(
                "%s: %d total, %d reserved",
                name.c_str(),
                holdings,
                reserved
            );
        }

        ImGui::End();

        ImGui::Begin("Ownership");

        if (ImGui::BeginTable("OwnershipTable", 5))
        {
            ImGui::TableSetupColumn("Instrument");
            ImGui::TableSetupColumn("Random");
            ImGui::TableSetupColumn("Mean");
            ImGui::TableSetupColumn("Rebalancer");
            ImGui::TableSetupColumn("Bank");

            ImGui::TableHeadersRow();

            for (int id : latest_snapshot.instrument_ids)
            {
                const auto& name =
                    latest_snapshot.instrument_names.at(id);

                int random = 0;
                int mean = 0;
                int rebalancer = 0;
                int bank = 0;

                if (latest_snapshot.random_percentage_of_each_instrument.contains(id))
                {
                    random =
                        latest_snapshot.random_percentage_of_each_instrument.at(id);
                }

                if (latest_snapshot.mean_reversion_percentage_of_each_instrument.contains(id))
                {
                    mean =
                        latest_snapshot.mean_reversion_percentage_of_each_instrument.at(id);
                }

                if (latest_snapshot.portfolio_rebalancer_percentage_of_each_instrument.contains(id))
                {
                    rebalancer =
                        latest_snapshot.portfolio_rebalancer_percentage_of_each_instrument.at(id);
                }

                if (latest_snapshot.bank_percentage_of_each_instrument.contains(id))
                {
                    bank =
                        latest_snapshot.bank_percentage_of_each_instrument.at(id);
                }

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d%%", random);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d%%", mean);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d%%", rebalancer);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d%%", bank);
            }

            ImGui::EndTable();
        }

        ImGui::End();

        ImGui::Begin("Deaths by Type");

        ImGui::Text(
            "Random: %d",
            latest_snapshot.random_deaths
        );

        ImGui::Text(
            "Mean value: %d",
            latest_snapshot.mean_reversion_deaths
        );

        ImGui::Text(
            "Rebalancer: %d",
            latest_snapshot.portfolio_rebalancer_deaths
        );

        ImGui::Separator();

        ImGui::Text(
            "Total: %d",
            latest_snapshot.total_deaths
        );

        ImGui::End();

        ImGui::Begin("Activity");

        ImGui::Text("Trades: %d",
                    latest_snapshot.total_trades);

        ImGui::Text("Active orders: %d",
                    latest_snapshot.active_orders);

        ImGui::Text("Traders created: %d",
                    latest_snapshot.replacements);

        ImGui::End();

        ImGui::Begin("Controls");

        if (ImGui::Button(running ? "Pause" : "Run"))
        {
            running = !running;

            if (running)
            {
                last_tick_time = glfwGetTime();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Step"))
        {
            sim.tick();
            record_snapshot();
        }

        ImGui::SliderFloat(
            "Ticks/sec",
            &ticks_per_second,
            1.0f,
            200.0f,
            "%.0f"
        );

        ImGui::End();
            
        ImGui::Render();

        int display_width;
        int display_height;
        glfwGetFramebufferSize(
            window,
            &display_width,
            &display_height
        );

        glViewport(0, 0, display_width, display_height);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData()
        );

        glfwSwapBuffers(window);
    }
    ImPlot::DestroyContext();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
