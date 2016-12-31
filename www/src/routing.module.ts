import { NgModule }             from '@angular/core';

import { RouterModule, Routes } from '@angular/router';

import { LoginComponent }       from './login.component';
import { ScreenComponent }      from './screen.component';
import { UsersComponent }       from './users.component';
import { HelpComponent }        from './help.component';

// Route Guards
import { LoginService }         from './login.service';
import { ScreenService }        from './screen.service';

// http://www.gistia.com/working-angular-2-router-part-1/
// https://angular.io/docs/ts/latest/guide/router.html
// http://jasonwatmore.com/post/2016/08/16/angular-2-jwt-authentication-example-tutorial
const routes = [
	{ path: '',            redirectTo: 'dashboard', pathMatch: 'full' },
	{ path: 'login',       component: LoginComponent },
	{ path: 'help',        component: HelpComponent, canActivate: [LoginService] },
	{ path: 'dashboard',   component: ScreenComponent, canActivate: [LoginService], resolve: { screen: ScreenService } },
	{ path: 'screen/:id',  component: ScreenComponent, canActivate: [LoginService], resolve: { screen: ScreenService } },
	{ path: 'users',       component: UsersComponent, canActivate: [LoginService] }
];

@NgModule( {
	imports:      [ RouterModule.forRoot( routes, { useHash: true } ) ],
	exports:      [ RouterModule ],
	providers:    [ ScreenService ]
} )

export class RoutingModule {
}
