import { NgModule }            from '@angular/core';
import {
	RouterModule,
	Routes
}                              from '@angular/router';

import { TimersListComponent } from './list.component';
import { TimerEditComponent }  from './edit.component';

import { TimersListResolver }  from './list.resolver';
import { TimerResolver }       from './timer.resolver';
import { ScriptsListResolver } from '../scripts/list.resolver';
import { DeviceResolver }      from '../devices/device.resolver';

import { SessionGuard }        from '../session/session.guard';

const routes: Routes = [
	{ path: 'timers',                             component: TimersListComponent, canActivate: [SessionGuard], resolve: { timers: TimersListResolver } },
	{ path: 'timers/:timer_id',                   component: TimerEditComponent,  canActivate: [SessionGuard], resolve: { timer: TimerResolver, scripts: ScriptsListResolver } },
	{ path: 'timers/:timer_id/device/:device_id', component: TimerEditComponent,  canActivate: [SessionGuard], resolve: { device: DeviceResolver, timer: TimerResolver } },
];

@NgModule( {
	imports: [
		RouterModule.forChild( routes )
	],
	exports: [
		RouterModule
	]
} )

export class TimersRoutingModule {
}
